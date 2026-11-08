/**
 * @file src/server/server.c
 * @brief Cloud-native HTTP micro-daemon for CDSL evaluation and metrics.
 */

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
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

#include "cdsl/server.h"
#include "cdsl/cdsl.h"
#include "cdsl/canary.h"
#include "cdsl/manager.h"
#include "cdsl/util/json.h"
#include "cdsl/util/threads.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline int
cdsl_http_strncasecmp(const char* s1, const char* s2, size_t n)
{
	for (size_t i = 0; i < n; i++) {
		unsigned char c1 = (unsigned char)s1[i];
		unsigned char c2 = (unsigned char)s2[i];
		if (tolower(c1) != tolower(c2)) {
			return tolower(c1) - tolower(c2);
		}
		if (c1 == '\0') {
			break;
		}
	}
	return 0;
}

static long
http_get_content_length(const char* buf, size_t len)
{
	const char* p = buf;
	const char* end = buf + len;
	while (p < end) {
		if (cdsl_http_strncasecmp(p, "Content-Length:", 15) == 0) {
			p += 15;
			while (p < end && (*p == ' ' || *p == '\t')) {
				p++;
			}
			return atol(p);
		}
		const char* next = memchr(p, '\n', (size_t)(end - p));
		if (!next) {
			break;
		}
		p = next + 1;
	}
	return -1;
}

typedef struct cdsl_server_metrics {
	uint64_t requests_total;
	uint64_t evaluations_total;
} cdsl_server_metrics_t;

struct cdsl_server {
	cdsl_server_config_t config;
	cdsl_socket_t server_fd;
	int actual_port;
	bool running;
	cdsl_thread_t worker_thread;
	cdsl_mutex_t lock;
	cdsl_server_metrics_t metrics;
	cdsl_manager_t* mgr;
};

static bool
server_is_running(cdsl_server_t* srv)
{
	if (!srv) {
		return false;
	}
	CDSL_MUTEX_LOCK(&srv->lock);
	bool r = srv->running;
	CDSL_MUTEX_UNLOCK(&srv->lock);
	return r;
}

static void
server_set_running(cdsl_server_t* srv, bool running)
{
	if (!srv) {
		return;
	}
	CDSL_MUTEX_LOCK(&srv->lock);
	srv->running = running;
	CDSL_MUTEX_UNLOCK(&srv->lock);
}

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

#ifdef _WIN32
	WSADATA wsa;
	(void)WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

	srv->mgr = cdsl_manager_create(NULL);

	return srv;
}

void
cdsl_server_free(cdsl_server_t* srv)
{
	if (!srv) {
		return;
	}
	if (server_is_running(srv)) {
		cdsl_server_stop(srv);
	}
	if (srv->mgr) {
		cdsl_manager_free(srv->mgr);
		srv->mgr = NULL;
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

cdsl_manager_t*
cdsl_server_get_manager(cdsl_server_t* srv)
{
	if (!srv) {
		return NULL;
	}
	return srv->mgr;
}

static char*
server_http_json_response(int status_code, const char* status_text, const char* json_body)
{
	size_t body_len = json_body ? strlen(json_body) : 0;
	size_t cap = body_len + 256;
	char* resp = malloc(cap);
	if (!resp) {
		return NULL;
	}
	snprintf(resp,
		 cap,
		 "HTTP/1.1 %d %s\r\nContent-Type: application/json\r\nContent-Length: "
		 "%zu\r\nConnection: close\r\n\r\n%s",
		 status_code,
		 status_text,
		 body_len,
		 json_body ? json_body : "");
	return resp;
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
		cdsl_json_value_t* json_root = cdsl_json_parse(body_start);
		const char* target_type = NULL;
		const char* target_dsl = NULL;

		if (json_root && json_root->type == CDSL_JSON_OBJECT) {
			/* Check for "type" field */
			cdsl_json_value_t* v_type = cdsl_json_get_object(json_root, "type");
			if (v_type && v_type->type == CDSL_JSON_STRING) {
				target_type = v_type->value.string_val;
			}

			/* Check for rule / source / dsl / dtable / scorecard / pipeline fields */
			cdsl_json_value_t* v_dsl = cdsl_json_get_object(json_root, "rule");
			if (!v_dsl) {
				v_dsl = cdsl_json_get_object(json_root, "dsl");
			}
			if (!v_dsl) {
				v_dsl = cdsl_json_get_object(json_root, "source");
			}
			if (!v_dsl) {
				v_dsl = cdsl_json_get_object(json_root, "content");
			}
			if (!v_dsl) {
				v_dsl = cdsl_json_get_object(json_root, "dtable");
			}
			if (!v_dsl) {
				v_dsl = cdsl_json_get_object(json_root, "scorecard");
			}
			if (!v_dsl) {
				v_dsl = cdsl_json_get_object(json_root, "pipeline");
			}
			if (v_dsl && v_dsl->type == CDSL_JSON_STRING) {
				target_dsl = v_dsl->value.string_val;
			}

			/* Populate context from "context" or "data" object, or flat root */
			cdsl_json_value_t* v_ctx = cdsl_json_get_object(json_root, "context");
			if (!v_ctx) {
				v_ctx = cdsl_json_get_object(json_root, "data");
			}
			if (v_ctx && v_ctx->type == CDSL_JSON_OBJECT) {
				for (cdsl_json_value_t* it = v_ctx->value.object.items; it;
				     it = it->next) {
					if (it->type == CDSL_JSON_NUMBER) {
						if ((double)(int64_t)it->value.number_val ==
						    it->value.number_val) {
							cdsl_context_set_long(
							    ctx,
							    it->key,
							    (int64_t)it->value.number_val);
						} else {
							cdsl_context_set_float(
							    ctx, it->key, it->value.number_val);
						}
					} else if (it->type == CDSL_JSON_BOOL) {
						cdsl_context_set_bool(
						    ctx, it->key, it->value.bool_val);
					} else if (it->type == CDSL_JSON_STRING) {
						cdsl_context_set_string(
						    ctx, it->key, it->value.string_val);
					}
				}
			} else {
				cdsl_context_load_json(ctx, body_start);
			}
		} else if (strlen(body_start) > 0) {
			cdsl_context_load_json(ctx, body_start);
		}

		const char* effective_dsl = target_dsl;
		if (!effective_dsl) {
			if (strstr(body_start, "RULE") != NULL ||
			    strstr(body_start, "DECISION_TABLE") != NULL ||
			    strstr(body_start, "SCORECARD") != NULL ||
			    strstr(body_start, "PIPELINE") != NULL) {
				effective_dsl = body_start;
			}
		}

		/* If still no effective DSL, check if srv->mgr has an active ruleset */
		if (!effective_dsl || strlen(effective_dsl) == 0) {
			if (srv->mgr && cdsl_manager_get_active_version(srv->mgr) > 0) {
				cdsl_vm_t* local_vm = cdsl_vm_create(NULL);
				cdsl_ruleset_report_t* rpt =
				    cdsl_manager_execute(srv->mgr, local_vm, ctx);
				cdsl_vm_free(local_vm);
				if (json_root) {
					cdsl_json_free(json_root);
				}
				cdsl_context_free(ctx);
				if (rpt) {
					char body[256];
					snprintf(body,
						 sizeof(body),
						 "{\"status\":\"%s\",\"total_passed\":%d,"
						 "\"total_failed\":%d}\n",
						 (rpt->total_failed == 0 && rpt->total_error == 0)
						     ? "PASSED"
						     : "FAILED",
						 rpt->total_passed,
						 rpt->total_failed);
					cdsl_ruleset_report_free(rpt);
					return server_http_json_response(200, "OK", body);
				}
				return server_http_json_response(
				    500,
				    "Internal Server Error",
				    "{\"error\":\"evaluation_failed\"}\n");
			}
			/* No active ruleset and no DSL in request -> reject with 400 */
			if (json_root) {
				cdsl_json_free(json_root);
			}
			cdsl_context_free(ctx);
			return server_http_json_response(
			    400,
			    "Bad Request",
			    "{\"error\":\"bad_request\",\"message\":\"missing rule definition or "
			    "evaluation target\"}\n");
		}

		/* Determine evaluation target via JSON field "type", with fallback to DSL prefix */
		char json_result[1024];
		int status_code;
		const char* status_text;

		if ((target_type && (strcmp(target_type, "dtable") == 0 ||
				     strcmp(target_type, "decision_table") == 0)) ||
		    (!target_type && strstr(effective_dsl, "DECISION_TABLE") != NULL)) {
			cdsl_dtable_t* dt = cdsl_dtable_parse_string(effective_dsl, NULL);
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
					status_code = 200;
					status_text = "OK";
				} else {
					snprintf(json_result,
						 sizeof(json_result),
						 "{\"error\":\"evaluation_failed\"}\n");
					status_code = 500;
					status_text = "Internal Server Error";
				}
				cdsl_dtable_free(dt);
			} else {
				snprintf(json_result,
					 sizeof(json_result),
					 "{\"error\":\"parse_error\",\"message\":\"failed to parse "
					 "decision table\"}\n");
				status_code = 400;
				status_text = "Bad Request";
			}
		} else if ((target_type && strcmp(target_type, "scorecard") == 0) ||
			   (!target_type && strstr(effective_dsl, "SCORECARD") != NULL)) {
			cdsl_scorecard_t* sc = cdsl_scorecard_parse_string(effective_dsl, NULL);
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
					status_code = 200;
					status_text = "OK";
				} else {
					snprintf(json_result,
						 sizeof(json_result),
						 "{\"error\":\"evaluation_failed\"}\n");
					status_code = 500;
					status_text = "Internal Server Error";
				}
				cdsl_scorecard_free(sc);
			} else {
				snprintf(json_result,
					 sizeof(json_result),
					 "{\"error\":\"parse_error\",\"message\":\"failed to parse "
					 "scorecard\"}\n");
				status_code = 400;
				status_text = "Bad Request";
			}
		} else if ((target_type && strcmp(target_type, "pipeline") == 0) ||
			   (!target_type && strstr(effective_dsl, "PIPELINE") != NULL)) {
			cdsl_pipeline_t* p = cdsl_pipeline_parse_string(effective_dsl, NULL);
			if (p) {
				cdsl_vm_t* local_vm = cdsl_vm_create(NULL);
				cdsl_pipeline_report_t* rpt =
				    cdsl_pipeline_execute(p, local_vm, ctx);
				if (rpt) {
					snprintf(json_result,
						 sizeof(json_result),
						 "{\"type\":\"pipeline\",\"pipeline_name\":\"%s\","
						 "\"status\":%d,\"stages\":%zu}\n",
						 rpt->pipeline_name ? rpt->pipeline_name : "",
						 rpt->status,
						 rpt->num_stages);
					cdsl_pipeline_report_free(rpt);
					status_code = 200;
					status_text = "OK";
				} else {
					snprintf(json_result,
						 sizeof(json_result),
						 "{\"error\":\"execution_failed\"}\n");
					status_code = 500;
					status_text = "Internal Server Error";
				}
				cdsl_vm_free(local_vm);
				cdsl_pipeline_free(p);
			} else {
				snprintf(json_result,
					 sizeof(json_result),
					 "{\"error\":\"parse_error\",\"message\":\"failed to parse "
					 "pipeline\"}\n");
				status_code = 400;
				status_text = "Bad Request";
			}
		} else {
			/* Standard Rule */
			const char* rule_dsl = strstr(effective_dsl, "RULE");
			if (!rule_dsl) {
				rule_dsl = effective_dsl;
			}
			cdsl_rule_t* rule = cdsl_parse_string(rule_dsl, NULL);
			if (rule) {
				cdsl_vm_t* local_vm = cdsl_vm_create(NULL);
				cdsl_rule_report_t* rpt = cdsl_vm_execute(local_vm, rule, ctx);
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
					status_code = 200;
					status_text = "OK";
				} else {
					snprintf(json_result,
						 sizeof(json_result),
						 "{\"error\":\"evaluation_failed\"}\n");
					status_code = 500;
					status_text = "Internal Server Error";
				}
				cdsl_vm_free(local_vm);
				cdsl_free_rule(rule);
			} else {
				snprintf(json_result,
					 sizeof(json_result),
					 "{\"error\":\"parse_error\",\"message\":\"failed to parse "
					 "rule\"}\n");
				status_code = 400;
				status_text = "Bad Request";
			}
		}

		if (json_root) {
			cdsl_json_free(json_root);
		}
		cdsl_context_free(ctx);

		return server_http_json_response(status_code, status_text, json_result);
	}

	/* Rule deploy endpoint */
	if (strcmp(method, "POST") == 0 && strcmp(path, "/v1/rules/deploy") == 0) {
		const char* body_start = strstr(http_request, "\r\n\r\n");
		if (body_start) {
			body_start += 4;
		} else {
			body_start = "";
		}

		const char* dsl_text = NULL;
		const char* tag = NULL;
		cdsl_json_value_t* json_root = cdsl_json_parse(body_start);
		if (json_root && json_root->type == CDSL_JSON_OBJECT) {
			cdsl_json_value_t* v_rule = cdsl_json_get_object(json_root, "rule");
			if (!v_rule) {
				v_rule = cdsl_json_get_object(json_root, "dsl");
			}
			if (!v_rule) {
				v_rule = cdsl_json_get_object(json_root, "source");
			}
			if (v_rule && v_rule->type == CDSL_JSON_STRING) {
				dsl_text = v_rule->value.string_val;
			}
			cdsl_json_value_t* v_tag = cdsl_json_get_object(json_root, "tag");
			if (v_tag && v_tag->type == CDSL_JSON_STRING) {
				tag = v_tag->value.string_val;
			}
		}

		if (!dsl_text && strstr(body_start, "RULE") != NULL) {
			dsl_text = body_start;
		}

		if (!dsl_text || strlen(dsl_text) == 0) {
			if (json_root) {
				cdsl_json_free(json_root);
			}
			return server_http_json_response(
			    400,
			    "Bad Request",
			    "{\"status\":\"rejected\",\"code\":1,\"error\":\"missing rule dsl to "
			    "deploy\"}\n");
		}

		CDSL_MUTEX_LOCK(&srv->lock);
		cdsl_canary_report_t rpt = {0};
		uint64_t version = 0;
		int ok = 0;
		if (srv->mgr) {
			ok = cdsl_manager_deploy_canary(
			    srv->mgr, dsl_text, tag, NULL, 0, &rpt, &version);
		}
		CDSL_MUTEX_UNLOCK(&srv->lock);

		if (json_root) {
			cdsl_json_free(json_root);
		}

		char resp_body[512];
		if (ok) {
			snprintf(resp_body,
				 sizeof(resp_body),
				 "{\"status\":\"deployed\",\"version\":%llu,\"code\":0}\n",
				 (unsigned long long)version);
		} else {
			snprintf(resp_body,
				 sizeof(resp_body),
				 "{\"status\":\"rejected\",\"code\":1,\"error\":\"%s\"}\n",
				 (rpt.message && strlen(rpt.message) > 0)
				     ? rpt.message
				     : "canary deployment failed");
		}
		cdsl_canary_report_free(&rpt);

		char resp[768];
		snprintf(resp,
			 sizeof(resp),
			 "HTTP/1.1 %s\r\nContent-Type: application/json\r\nContent-Length: "
			 "%zu\r\nConnection: close\r\n\r\n%s",
			 ok ? "200 OK" : "400 Bad Request",
			 strlen(resp_body),
			 resp_body);
		return cdsl_strdup(resp);
	}

	return cdsl_strdup(
	    "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
}

static void*
server_worker_loop(void* arg)
{
	cdsl_server_t* srv = (cdsl_server_t*)arg;

	while (server_is_running(srv)) {
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
		if (r <= 0 || !server_is_running(srv)) {
			continue;
		}

		struct sockaddr_in client_addr;
		socklen_t client_len = sizeof(client_addr);
		cdsl_socket_t client_fd =
		    accept(srv->server_fd, (struct sockaddr*)&client_addr, &client_len);
		if (client_fd == CDSL_INVALID_SOCKET) {
			continue;
		}

		size_t cap = 4096;
		size_t len = 0;
		char* req_buf = malloc(cap);
		if (!req_buf) {
			CDSL_CLOSE_SOCKET(client_fd);
			continue;
		}

		long content_len = -1;
		const char* header_end = NULL;

		while (server_is_running(srv)) {
			if (len + 1024 >= cap) {
				if (cap >= 10 * 1024 * 1024) { /* 10MB safety threshold */
					break;
				}
				cap *= 2;
				char* new_buf = realloc(req_buf, cap);
				if (!new_buf) {
					break;
				}
				req_buf = new_buf;
			}
			int n = recv(client_fd, req_buf + len, (int)(cap - 1 - len), 0);
			if (n <= 0) {
				break;
			}
			len += (size_t)n;
			req_buf[len] = '\0';

			if (!header_end) {
				header_end = strstr(req_buf, "\r\n\r\n");
				if (!header_end) {
					header_end = strstr(req_buf, "\n\n");
				}
				if (header_end) {
					content_len = http_get_content_length(
					    req_buf, (size_t)(header_end - req_buf));
				}
			}

			if (header_end) {
				size_t h_len = (size_t)(header_end - req_buf) +
					       (header_end[0] == '\r' ? 4 : 2);
				if (content_len <= 0 || len >= h_len + (size_t)content_len) {
					break;
				}
			}
		}

		if (len > 0) {
			char* resp = cdsl_server_handle_request(srv, req_buf);
			if (resp) {
				(void)send(client_fd, resp, (int)strlen(resp), 0);
				free(resp);
			}
		}
		free(req_buf);
		CDSL_CLOSE_SOCKET(client_fd);
	}

	return NULL;
}

int
cdsl_server_start(cdsl_server_t* srv)
{
	if (!srv || server_is_running(srv)) {
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

	server_set_running(srv, true);
	if (CDSL_THREAD_CREATE(&srv->worker_thread, server_worker_loop, srv) != 0) {
		server_set_running(srv, false);
		CDSL_CLOSE_SOCKET(srv->server_fd);
		srv->server_fd = CDSL_INVALID_SOCKET;
		return -1;
	}

	return 0;
}

void
cdsl_server_stop(cdsl_server_t* srv)
{
	if (!srv || !server_is_running(srv)) {
		return;
	}
	server_set_running(srv, false);
	CDSL_THREAD_JOIN(srv->worker_thread);
	if (srv->server_fd != CDSL_INVALID_SOCKET) {
		CDSL_CLOSE_SOCKET(srv->server_fd);
		srv->server_fd = CDSL_INVALID_SOCKET;
	}
}
