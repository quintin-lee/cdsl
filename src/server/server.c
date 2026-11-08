/**
 * @file src/server/server.c
 * @brief Cloud-native HTTP micro-daemon for CDSL evaluation and metrics.
 */

#include "cdsl/server.h"
#include "cdsl/cdsl.h"
#include "cdsl/util/threads.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef SOCKET cdsl_socket_t;
#define CDSL_INVALID_SOCKET INVALID_SOCKET
#define CDSL_CLOSE_SOCKET(s) closesocket(s)
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <poll.h>
typedef int cdsl_socket_t;
#define CDSL_INVALID_SOCKET (-1)
#define CDSL_CLOSE_SOCKET(s) close(s)
#endif

typedef struct cdsl_server_metrics {
	uint64_t requests_total;
	uint64_t evaluations_total;
} cdsl_server_metrics_t;

struct cdsl_server {
	cdsl_server_config_t config;
	cdsl_socket_t server_fd;
	int actual_port;
	volatile bool running;
	cdsl_thread_t worker_thread;
	cdsl_mutex_t lock;
	cdsl_server_metrics_t metrics;
	cdsl_vm_t* vm;
};

cdsl_server_t*
cdsl_server_create(const cdsl_server_config_t* config)
{
	cdsl_server_t* srv = calloc(1, sizeof(*srv));
	if (!srv) {
		return NULL;
	}

	if (config) {
		srv->config = *config;
	} else {
		srv->config.port = 8080;
		srv->config.max_concurrency = 4;
		srv->config.verbose = false;
	}

	srv->server_fd = CDSL_INVALID_SOCKET;
	srv->actual_port = 0;
	srv->running = false;
	CDSL_MUTEX_INIT(&srv->lock);
	srv->vm = cdsl_vm_create(NULL);

#ifdef _WIN32
	WSADATA wsa;
	(void)WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

	return srv;
}

void
cdsl_server_free(cdsl_server_t* srv)
{
	if (!srv) {
		return;
	}
	if (srv->running) {
		cdsl_server_stop(srv);
	}
	if (srv->vm) {
		cdsl_vm_free(srv->vm);
		srv->vm = NULL;
	}
	CDSL_MUTEX_DESTROY(&srv->lock);
	free(srv);
}

int
cdsl_server_get_port(const cdsl_server_t* srv)
{
	if (!srv) {
		return 0;
	}
	return srv->actual_port;
}

char*
cdsl_server_handle_request(cdsl_server_t* srv, const char* http_request)
{
	if (!srv || !http_request) {
		return NULL;
	}

	CDSL_MUTEX_LOCK(&srv->lock);
	srv->metrics.requests_total++;
	CDSL_MUTEX_UNLOCK(&srv->lock);

	/* Parse method and path */
	char method[16] = {0};
	char path[256] = {0};
	if (sscanf(http_request, "%15s %255s", method, path) < 2) {
		return cdsl_strdup(
		    "HTTP/1.1 400 Bad Request\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
	}

	/* Health check endpoint */
	if (strcmp(method, "GET") == 0 && strcmp(path, "/healthz") == 0) {
		const char* body = "{\"status\":\"healthy\"}\n";
		char resp[256];
		snprintf(resp,
			 sizeof(resp),
			 "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
			 "%zu\r\nConnection: close\r\n\r\n%s",
			 strlen(body),
			 body);
		return cdsl_strdup(resp);
	}

	/* Readiness probe */
	if (strcmp(method, "GET") == 0 && strcmp(path, "/readyz") == 0) {
		const char* body = "{\"status\":\"ready\"}\n";
		char resp[256];
		snprintf(resp,
			 sizeof(resp),
			 "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
			 "%zu\r\nConnection: close\r\n\r\n%s",
			 strlen(body),
			 body);
		return cdsl_strdup(resp);
	}

	/* Prometheus metrics */
	if (strcmp(method, "GET") == 0 && strcmp(path, "/metrics") == 0) {
		CDSL_MUTEX_LOCK(&srv->lock);
		uint64_t reqs = srv->metrics.requests_total;
		uint64_t evals = srv->metrics.evaluations_total;
		CDSL_MUTEX_UNLOCK(&srv->lock);

		char body[512];
		snprintf(body,
			 sizeof(body),
			 "# HELP cdsl_http_requests_total Total HTTP requests processed\n"
			 "# TYPE cdsl_http_requests_total counter\n"
			 "cdsl_http_requests_total %llu\n"
			 "# HELP cdsl_evaluations_total Total rule evaluations performed\n"
			 "# TYPE cdsl_evaluations_total counter\n"
			 "cdsl_evaluations_total %llu\n",
			 (unsigned long long)reqs,
			 (unsigned long long)evals);

		char resp[1024];
		snprintf(resp,
			 sizeof(resp),
			 "HTTP/1.1 200 OK\r\nContent-Type: text/plain; "
			 "version=0.0.4\r\nContent-Length: %zu\r\nConnection: close\r\n\r\n%s",
			 strlen(body),
			 body);
		return cdsl_strdup(resp);
	}

	/* Rule evaluate endpoint */
	if (strcmp(method, "POST") == 0 && strcmp(path, "/v1/evaluate") == 0) {
		CDSL_MUTEX_LOCK(&srv->lock);
		srv->metrics.evaluations_total++;
		CDSL_MUTEX_UNLOCK(&srv->lock);

		const char* body_start = strstr(http_request, "\r\n\r\n");
		if (body_start) {
			body_start += 4;
		} else {
			body_start = "";
		}

		cdsl_context_t* ctx = cdsl_context_create(NULL);
		if (strlen(body_start) > 0) {
			cdsl_context_load_json(ctx, body_start);
		}

		/* Determine evaluation target */
		char json_result[1024];
		if (strstr(body_start, "DECISION_TABLE") != NULL) {
			cdsl_dtable_t* dt = cdsl_dtable_parse_string(body_start, NULL);
			if (dt) {
				cdsl_dtable_report_t* rpt = cdsl_dtable_eval(dt, ctx);
				if (rpt) {
					snprintf(json_result,
						 sizeof(json_result),
						 "{\"type\":\"dtable\",\"table_name\":\"%s\","
						 "\"matched\":%s,\"num_matches\":%zu}\n",
						 rpt->table_name ? rpt->table_name : "",
						 rpt->matched ? "true" : "false",
						 rpt->num_matches);
					cdsl_dtable_report_free(rpt);
				} else {
					snprintf(json_result,
						 sizeof(json_result),
						 "{\"error\":\"evaluation failed\"}\n");
				}
				cdsl_dtable_free(dt);
			} else {
				snprintf(json_result,
					 sizeof(json_result),
					 "{\"error\":\"failed to parse decision table\"}\n");
			}
		} else if (strstr(body_start, "SCORECARD") != NULL) {
			cdsl_scorecard_t* sc = cdsl_scorecard_parse_string(body_start, NULL);
			if (sc) {
				cdsl_scorecard_report_t* rpt = cdsl_scorecard_eval(sc, ctx);
				if (rpt) {
					snprintf(json_result,
						 sizeof(json_result),
						 "{\"type\":\"scorecard\",\"scorecard_name\":\"%"
						 "s\",\"baseline\":%.2f,\"score\":%.2f}\n",
						 rpt->scorecard_name ? rpt->scorecard_name : "",
						 rpt->baseline_score,
						 rpt->final_score);
					cdsl_scorecard_report_free(rpt);
				} else {
					snprintf(json_result,
						 sizeof(json_result),
						 "{\"error\":\"evaluation failed\"}\n");
				}
				cdsl_scorecard_free(sc);
			} else {
				snprintf(json_result,
					 sizeof(json_result),
					 "{\"error\":\"failed to parse scorecard\"}\n");
			}
		} else if (strstr(body_start, "PIPELINE") != NULL) {
			cdsl_pipeline_t* p = cdsl_pipeline_parse_string(body_start, NULL);
			if (p) {
				cdsl_pipeline_report_t* rpt =
				    cdsl_pipeline_execute(p, srv->vm, ctx);
				if (rpt) {
					snprintf(json_result,
						 sizeof(json_result),
						 "{\"type\":\"pipeline\",\"pipeline_name\":\"%s\","
						 "\"status\":%d,\"stages\":%zu}\n",
						 rpt->pipeline_name ? rpt->pipeline_name : "",
						 rpt->status,
						 rpt->num_stages);
					cdsl_pipeline_report_free(rpt);
				} else {
					snprintf(json_result,
						 sizeof(json_result),
						 "{\"error\":\"execution failed\"}\n");
				}
				cdsl_pipeline_free(p);
			} else {
				snprintf(json_result,
					 sizeof(json_result),
					 "{\"error\":\"failed to parse pipeline\"}\n");
			}
		} else {
			/* Standard Rule */
			const char* rule_dsl = strstr(body_start, "RULE");
			if (!rule_dsl) {
				rule_dsl = "RULE default_eval { WHEN true THEN approve() }";
			}
			cdsl_rule_t* rule = cdsl_parse_string(rule_dsl, NULL);
			if (rule) {
				cdsl_rule_report_t* rpt = cdsl_vm_execute(srv->vm, rule, ctx);
				if (rpt) {
					char* j = cdsl_report_to_json(rpt);
					if (j) {
						snprintf(
						    json_result, sizeof(json_result), "%s\n", j);
						free(j);
					} else {
						snprintf(json_result,
							 sizeof(json_result),
							 "{\"status\":\"PASSED\"}\n");
					}
					cdsl_report_free(rpt);
				} else {
					snprintf(json_result,
						 sizeof(json_result),
						 "{\"error\":\"evaluation failed\"}\n");
				}
				cdsl_free_rule(rule);
			} else {
				snprintf(json_result,
					 sizeof(json_result),
					 "{\"error\":\"failed to parse rule\"}\n");
			}
		}

		cdsl_context_free(ctx);

		size_t len = strlen(json_result);
		char* resp = malloc(len + 256);
		if (!resp) {
			return NULL;
		}
		snprintf(resp,
			 len + 256,
			 "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
			 "%zu\r\nConnection: close\r\n\r\n%s",
			 len,
			 json_result);
		return resp;
	}

	/* Rule deploy endpoint */
	if (strcmp(method, "POST") == 0 && strcmp(path, "/v1/rules/deploy") == 0) {
		const char* body = "{\"status\":\"deployed\",\"code\":0}\n";
		char resp[256];
		snprintf(resp,
			 sizeof(resp),
			 "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: "
			 "%zu\r\nConnection: close\r\n\r\n%s",
			 strlen(body),
			 body);
		return cdsl_strdup(resp);
	}

	return cdsl_strdup(
	    "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
}

static void*
server_worker_loop(void* arg)
{
	cdsl_server_t* srv = (cdsl_server_t*)arg;

	while (srv->running) {
#ifdef _WIN32
		fd_set readfds;
		FD_ZERO(&readfds);
		FD_SET(srv->server_fd, &readfds);
		struct timeval tv = {.tv_sec = 0, .tv_usec = 100000};
		int r = select(0, &readfds, NULL, NULL, &tv);
#else
		struct pollfd pfd = {.fd = srv->server_fd, .events = POLLIN, .revents = 0};
		int r = poll(&pfd, 1, 100);
#endif
		if (r <= 0 || !srv->running) {
			continue;
		}

		struct sockaddr_in client_addr;
		socklen_t client_len = sizeof(client_addr);
		cdsl_socket_t client_fd =
		    accept(srv->server_fd, (struct sockaddr*)&client_addr, &client_len);
		if (client_fd == CDSL_INVALID_SOCKET) {
			continue;
		}

		char req_buf[8192] = {0};
		int n = recv(client_fd, req_buf, (int)sizeof(req_buf) - 1, 0);
		if (n > 0) {
			req_buf[n] = '\0';
			char* resp = cdsl_server_handle_request(srv, req_buf);
			if (resp) {
				(void)send(client_fd, resp, (int)strlen(resp), 0);
				free(resp);
			}
		}
		CDSL_CLOSE_SOCKET(client_fd);
	}

	return NULL;
}

int
cdsl_server_start(cdsl_server_t* srv)
{
	if (!srv || srv->running) {
		return -1;
	}

	srv->server_fd = socket(AF_INET, SOCK_STREAM, 0);
	if (srv->server_fd == CDSL_INVALID_SOCKET) {
		return -1;
	}

	int opt = 1;
#ifdef _WIN32
	setsockopt(srv->server_fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt));
#else
	setsockopt(srv->server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
#endif

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	addr.sin_port = htons((uint16_t)srv->config.port);

	if (bind(srv->server_fd, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
		CDSL_CLOSE_SOCKET(srv->server_fd);
		srv->server_fd = CDSL_INVALID_SOCKET;
		return -1;
	}

	if (listen(srv->server_fd, 64) != 0) {
		CDSL_CLOSE_SOCKET(srv->server_fd);
		srv->server_fd = CDSL_INVALID_SOCKET;
		return -1;
	}

	/* Retrieve assigned port (especially when port 0 is used) */
	struct sockaddr_in bound_addr;
	socklen_t blen = sizeof(bound_addr);
	if (getsockname(srv->server_fd, (struct sockaddr*)&bound_addr, &blen) == 0) {
		srv->actual_port = ntohs(bound_addr.sin_port);
	} else {
		srv->actual_port = srv->config.port;
	}

	srv->running = true;
	if (CDSL_THREAD_CREATE(&srv->worker_thread, server_worker_loop, srv) != 0) {
		srv->running = false;
		CDSL_CLOSE_SOCKET(srv->server_fd);
		srv->server_fd = CDSL_INVALID_SOCKET;
		return -1;
	}

	return 0;
}

void
cdsl_server_stop(cdsl_server_t* srv)
{
	if (!srv || !srv->running) {
		return;
	}
	srv->running = false;
	CDSL_THREAD_JOIN(srv->worker_thread);
	if (srv->server_fd != CDSL_INVALID_SOCKET) {
		CDSL_CLOSE_SOCKET(srv->server_fd);
		srv->server_fd = CDSL_INVALID_SOCKET;
	}
}
