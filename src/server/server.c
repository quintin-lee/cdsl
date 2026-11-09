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
#if !defined(_SSIZE_T_DEFINED) && !defined(_SSIZE_T_)
typedef ptrdiff_t ssize_t;
#define _SSIZE_T_DEFINED
#define _SSIZE_T_
#endif
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
			return strtol(p, NULL, 10);
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

#define CDSL_SERVER_QUEUE_CAP 128
/* Upper bound on HTTP request bodies: the client's Content-Length beyond
 * this is rejected with 413 Payload Too Large before the read loop runs,
 * and the in-flight buffer expansion loop caps at this size. */
#define CDSL_SERVER_MAX_BODY_BYTES 10485760ul /* 10 MB */

struct cdsl_server {
	cdsl_server_config_t config;
	cdsl_socket_t server_fd;
	int actual_port;
	bool running;
	cdsl_thread_t listener_thread;
	cdsl_thread_t* worker_threads;
	size_t num_workers;
	cdsl_mutex_t lock;
	cdsl_server_metrics_t metrics;
	cdsl_manager_t* mgr;

	/* Bounded connection queue for max_concurrency workers */
	cdsl_socket_t queue[CDSL_SERVER_QUEUE_CAP];
	size_t queue_head;
	size_t queue_tail;
	size_t queue_count;
	cdsl_mutex_t queue_lock;
	cdsl_cond_t queue_not_empty;
	cdsl_cond_t queue_not_full;
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

	if (srv->config.max_concurrency == 0) {
		srv->config.max_concurrency = 4;
	}

	srv->server_fd = CDSL_INVALID_SOCKET;
	srv->actual_port = 0;
	srv->running = false;
	CDSL_MUTEX_INIT(&srv->lock);
	CDSL_MUTEX_INIT(&srv->queue_lock);
	CDSL_COND_INIT(&srv->queue_not_empty);
	CDSL_COND_INIT(&srv->queue_not_full);
	srv->queue_head = 0;
	srv->queue_tail = 0;
	srv->queue_count = 0;
	srv->worker_threads = NULL;
	srv->num_workers = 0;

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
	CDSL_MUTEX_DESTROY(&srv->queue_lock);
	CDSL_COND_DESTROY(&srv->queue_not_empty);
	CDSL_COND_DESTROY(&srv->queue_not_full);
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
server_http_response(int status_code,
		     const char* status_text,
		     const char* content_type,
		     const char* body)
{
	size_t body_len = body ? strlen(body) : 0;
	size_t hdr_est = 256 + (content_type ? strlen(content_type) : 0) +
			 (status_text ? strlen(status_text) : 0);
	size_t cap = body_len + hdr_est;
	char* resp = malloc(cap);
	if (!resp) {
		return NULL;
	}
	snprintf(resp,
		 cap,
		 "HTTP/1.1 %d %s\r\n"
		 "Content-Type: %s\r\n"
		 "Content-Length: %zu\r\n"
		 "Connection: close\r\n\r\n%s",
		 status_code,
		 status_text ? status_text : "OK",
		 content_type ? content_type : "application/json",
		 body_len,
		 body ? body : "");
	return resp;
}

static char*
server_http_json_response(int status_code, const char* status_text, const char* json_body)
{
	return server_http_response(status_code, status_text, "application/json", json_body);
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
		return server_http_json_response(200, "OK", "{\"status\":\"healthy\"}\n");
	}

	/* Readiness probe */
	if (strcmp(method, "GET") == 0 && strcmp(path, "/readyz") == 0) {
		return server_http_json_response(200, "OK", "{\"status\":\"ready\"}\n");
	}

	/* Prometheus metrics */
	if (strcmp(method, "GET") == 0 && strcmp(path, "/metrics") == 0) {
		CDSL_MUTEX_LOCK(&srv->lock);
		uint64_t reqs = srv->metrics.requests_total;
		uint64_t evals = srv->metrics.evaluations_total;
		CDSL_MUTEX_UNLOCK(&srv->lock);

		size_t body_cap = 512;
		char* body = malloc(body_cap);
		if (!body) {
			return server_http_response(
			    500, "Internal Server Error", "text/plain", "OOM\n");
		}
		snprintf(body,
			 body_cap,
			 "# HELP cdsl_http_requests_total Total HTTP requests processed\n"
			 "# TYPE cdsl_http_requests_total counter\n"
			 "cdsl_http_requests_total %llu\n"
			 "# HELP cdsl_evaluations_total Total rule evaluations performed\n"
			 "# TYPE cdsl_evaluations_total counter\n"
			 "cdsl_evaluations_total %llu\n",
			 (unsigned long long)reqs,
			 (unsigned long long)evals);

		char* resp = server_http_response(200, "OK", "text/plain; version=0.0.4", body);
		free(body);
		return resp;
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
						if (it->is_int) {
							cdsl_context_set_long(
							    ctx, it->key, it->value.i64);
						} else {
							cdsl_context_set_float(
							    ctx, it->key, it->value.f64);
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
					size_t body_cap = 256;
					char* body = malloc(body_cap);
					if (body) {
						snprintf(body,
							 body_cap,
							 "{\"status\":\"%s\",\"total_passed\":%d,"
							 "\"total_failed\":%d}\n",
							 (rpt->total_failed == 0 &&
							  rpt->total_error == 0)
							     ? "PASSED"
							     : "FAILED",
							 rpt->total_passed,
							 rpt->total_failed);
						char* resp =
						    server_http_json_response(200, "OK", body);
						free(body);
						cdsl_ruleset_report_free(rpt);
						return resp;
					}
					cdsl_ruleset_report_free(rpt);
					return server_http_json_response(
					    500, "Internal Server Error", "{\"error\":\"oom\"}\n");
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
		char* json_result = NULL;
		int status_code = 500;
		const char* status_text = "Internal Server Error";

		if ((target_type && (strcmp(target_type, "dtable") == 0 ||
				     strcmp(target_type, "decision_table") == 0)) ||
		    (!target_type && strstr(effective_dsl, "DECISION_TABLE") != NULL)) {
			cdsl_dtable_t* dt = cdsl_dtable_parse_string(effective_dsl, NULL);
			if (dt) {
				cdsl_dtable_report_t* rpt = cdsl_dtable_eval(dt, ctx);
				if (rpt) {
					size_t cap =
					    256 + (rpt->table_name ? strlen(rpt->table_name) : 0);
					json_result = malloc(cap);
					if (json_result) {
						snprintf(
						    json_result,
						    cap,
						    "{\"type\":\"dtable\",\"table_name\":\"%s\","
						    "\"matched\":%s,\"num_matches\":%zu}\n",
						    rpt->table_name ? rpt->table_name : "",
						    rpt->matched ? "true" : "false",
						    rpt->num_matches);
						status_code = 200;
						status_text = "OK";
					}
					cdsl_dtable_report_free(rpt);
				}
				cdsl_dtable_free(dt);
			} else {
				status_code = 400;
				status_text = "Bad Request";
				json_result = cdsl_strdup(
				    "{\"error\":\"parse_error\",\"message\":\"failed to parse "
				    "decision table\"}\n");
			}
		} else if ((target_type && strcmp(target_type, "scorecard") == 0) ||
			   (!target_type && strstr(effective_dsl, "SCORECARD") != NULL)) {
			cdsl_scorecard_t* sc = cdsl_scorecard_parse_string(effective_dsl, NULL);
			if (sc) {
				cdsl_scorecard_report_t* rpt = cdsl_scorecard_eval(sc, ctx);
				if (rpt) {
					size_t cap =
					    256 +
					    (rpt->scorecard_name ? strlen(rpt->scorecard_name) : 0);
					json_result = malloc(cap);
					if (json_result) {
						snprintf(
						    json_result,
						    cap,
						    "{\"type\":\"scorecard\",\"scorecard_name\":\"%"
						    "s\",\"baseline\":%.2f,\"score\":%.2f}\n",
						    rpt->scorecard_name ? rpt->scorecard_name : "",
						    rpt->baseline_score,
						    rpt->final_score);
						status_code = 200;
						status_text = "OK";
					}
					cdsl_scorecard_report_free(rpt);
				}
				cdsl_scorecard_free(sc);
			} else {
				status_code = 400;
				status_text = "Bad Request";
				json_result = cdsl_strdup(
				    "{\"error\":\"parse_error\",\"message\":\"failed to parse "
				    "scorecard\"}\n");
			}
		} else if ((target_type && strcmp(target_type, "pipeline") == 0) ||
			   (!target_type && strstr(effective_dsl, "PIPELINE") != NULL)) {
			cdsl_pipeline_t* p = cdsl_pipeline_parse_string(effective_dsl, NULL);
			if (p) {
				cdsl_vm_t* local_vm = cdsl_vm_create(NULL);
				cdsl_pipeline_report_t* rpt =
				    cdsl_pipeline_execute(p, local_vm, ctx);
				if (rpt) {
					size_t cap =
					    256 +
					    (rpt->pipeline_name ? strlen(rpt->pipeline_name) : 0);
					json_result = malloc(cap);
					if (json_result) {
						snprintf(json_result,
							 cap,
							 "{\"type\":\"pipeline\",\"pipeline_name\":"
							 "\"%s\","
							 "\"status\":%d,\"stages\":%zu}\n",
							 rpt->pipeline_name ? rpt->pipeline_name
									    : "",
							 rpt->status,
							 rpt->num_stages);
						status_code = 200;
						status_text = "OK";
					}
					cdsl_pipeline_report_free(rpt);
				}
				cdsl_vm_free(local_vm);
				cdsl_pipeline_free(p);
			} else {
				status_code = 400;
				status_text = "Bad Request";
				json_result = cdsl_strdup(
				    "{\"error\":\"parse_error\",\"message\":\"failed to parse "
				    "pipeline\"}\n");
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
						size_t jlen = strlen(j);
						json_result = malloc(jlen + 2);
						if (json_result) {
							memcpy(json_result, j, jlen);
							json_result[jlen] = '\n';
							json_result[jlen + 1] = '\0';
						}
						free(j);
					} else {
						json_result =
						    cdsl_strdup("{\"status\":\"PASSED\"}\n");
					}
					cdsl_report_free(rpt);
					status_code = 200;
					status_text = "OK";
				}
				cdsl_vm_free(local_vm);
				cdsl_free_rule(rule);
			} else {
				status_code = 400;
				status_text = "Bad Request";
				json_result = cdsl_strdup(
				    "{\"error\":\"parse_error\",\"message\":\"failed to parse "
				    "rule\"}\n");
			}
		}

		if (!json_result) {
			json_result = cdsl_strdup("{\"error\":\"evaluation_failed\"}\n");
		}

		if (json_root) {
			cdsl_json_free(json_root);
		}
		cdsl_context_free(ctx);

		char* resp = server_http_json_response(status_code, status_text, json_result);
		free(json_result);
		return resp;
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

		size_t resp_cap = 512 + (rpt.message ? strlen(rpt.message) : 0);
		char* resp_body = malloc(resp_cap);
		if (resp_body) {
			if (ok) {
				snprintf(resp_body,
					 resp_cap,
					 "{\"status\":\"deployed\",\"version\":%llu,\"code\":0}\n",
					 (unsigned long long)version);
			} else {
				snprintf(resp_body,
					 resp_cap,
					 "{\"status\":\"rejected\",\"code\":1,\"error\":\"%s\"}\n",
					 (rpt.message && strlen(rpt.message) > 0)
					     ? rpt.message
					     : "canary deployment failed");
			}
		}
		cdsl_canary_report_free(&rpt);

		char* resp = server_http_json_response(
		    ok ? 200 : 400, ok ? "OK" : "Bad Request", resp_body ? resp_body : "");
		free(resp_body);
		return resp;
	}

	return cdsl_strdup(
	    "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
}

static void
server_handle_client(cdsl_server_t* srv, cdsl_socket_t client_fd)
{
	size_t cap = 4096;
	size_t len = 0;
	char* req_buf = malloc(cap);
	if (!req_buf) {
		CDSL_CLOSE_SOCKET(client_fd);
		return;
	}

	long content_len = -1;
	const char* header_end = NULL;

	/* In-flight connections must be drained to completion regardless of the
	 * running flag: stop() only decides when the worker exits AFTER this
	 * connection finishes, it must not abort the request read mid-flight */
	while (1) {
		if (len + 1024 >= cap) {
			if (cap >= CDSL_SERVER_MAX_BODY_BYTES) {
				break;
			}
			cap *= 2;
			char* new_buf = realloc(req_buf, cap);
			if (!new_buf) {
				break;
			}
			req_buf = new_buf;
		}
		ssize_t n = recv(client_fd, req_buf + len, (int)(cap - 1 - len), 0);
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
				if (content_len > 0 &&
				    (size_t)content_len > CDSL_SERVER_MAX_BODY_BYTES) {
					/* Reject oversized bodies before the read loop
					 * so a forged Content-Length cannot drive the
					 * buffer to its 10MB cap with an unsatisfiable
					 * total-length check. */
					send(client_fd, "HTTP/1.1 413 Payload Too Large\r\n"
					     "Content-Length: 0\r\nConnection: close\r\n\r\n",
				     72, 0);
					free(req_buf);
					CDSL_CLOSE_SOCKET(client_fd);
					return;
				}
			}
		}

		if (header_end) {
			size_t h_len =
			    (size_t)(header_end - req_buf) + (header_end[0] == '\r' ? 4 : 2);
			if (content_len <= 0 || len >= h_len + (size_t)content_len) {
				break;
			}
		}
	}

	if (len > 0) {
		char* resp = cdsl_server_handle_request(srv, req_buf);
		if (resp) {
			size_t total_to_send = strlen(resp);
			size_t sent = 0;
			while (sent < total_to_send) {
				ssize_t n =
				    send(client_fd, resp + sent, (int)(total_to_send - sent), 0);
				if (n <= 0) {
					break;
				}
				sent += (size_t)n;
			}
			free(resp);
		}
	}
	free(req_buf);
	CDSL_CLOSE_SOCKET(client_fd);
}

static void*
server_worker_routine(void* arg)
{
	cdsl_server_t* srv = (cdsl_server_t*)arg;

	while (1) {
		cdsl_socket_t client_fd = CDSL_INVALID_SOCKET;

		CDSL_MUTEX_LOCK(&srv->queue_lock);
		while (server_is_running(srv) && srv->queue_count == 0) {
			CDSL_COND_WAIT(&srv->queue_not_empty, &srv->queue_lock);
		}
		if (!server_is_running(srv) && srv->queue_count == 0) {
			CDSL_MUTEX_UNLOCK(&srv->queue_lock);
			break;
		}
		client_fd = srv->queue[srv->queue_head];
		srv->queue_head = (srv->queue_head + 1) % CDSL_SERVER_QUEUE_CAP;
		srv->queue_count--;
		CDSL_COND_SIGNAL(&srv->queue_not_full);
		CDSL_MUTEX_UNLOCK(&srv->queue_lock);

		if (client_fd != CDSL_INVALID_SOCKET) {
			server_handle_client(srv, client_fd);
		}
	}

	return NULL;
}

static void*
server_listener_routine(void* arg)
{
	cdsl_server_t* srv = (cdsl_server_t*)arg;

	while (server_is_running(srv)) {
		if (srv->server_fd == CDSL_INVALID_SOCKET) {
			break;
		}
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

		CDSL_MUTEX_LOCK(&srv->queue_lock);
		while (server_is_running(srv) && srv->queue_count == CDSL_SERVER_QUEUE_CAP) {
			CDSL_COND_WAIT(&srv->queue_not_full, &srv->queue_lock);
		}
		if (!server_is_running(srv)) {
			/* Graceful drain: hand off an already-accepted connection to a
			 * worker instead of dropping it, so in-flight requests still
			 * complete after stop() was requested. Only close when the queue
			 * is full and no worker can take the fd. */
			int enqueued = 0;
			if (srv->queue_count < CDSL_SERVER_QUEUE_CAP) {
				srv->queue[srv->queue_tail] = client_fd;
				srv->queue_tail = (srv->queue_tail + 1) % CDSL_SERVER_QUEUE_CAP;
				srv->queue_count++;
				CDSL_COND_SIGNAL(&srv->queue_not_empty);
				enqueued = 1;
			}
			CDSL_MUTEX_UNLOCK(&srv->queue_lock);
			if (!enqueued) {
				CDSL_CLOSE_SOCKET(client_fd);
			}
			break;
		}
		srv->queue[srv->queue_tail] = client_fd;
		srv->queue_tail = (srv->queue_tail + 1) % CDSL_SERVER_QUEUE_CAP;
		srv->queue_count++;
		CDSL_COND_SIGNAL(&srv->queue_not_empty);
		CDSL_MUTEX_UNLOCK(&srv->queue_lock);
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

	size_t workers = srv->config.max_concurrency > 0 ? srv->config.max_concurrency : 4;
	srv->worker_threads = calloc(workers, sizeof(cdsl_thread_t));
	if (!srv->worker_threads) {
		server_set_running(srv, false);
		CDSL_CLOSE_SOCKET(srv->server_fd);
		srv->server_fd = CDSL_INVALID_SOCKET;
		return -1;
	}

	srv->num_workers = 0;
	for (size_t i = 0; i < workers; i++) {
		if (CDSL_THREAD_CREATE(&srv->worker_threads[i], server_worker_routine, srv) == 0) {
			srv->num_workers++;
		}
	}

	if (srv->num_workers == 0) {
		server_set_running(srv, false);
		free(srv->worker_threads);
		srv->worker_threads = NULL;
		CDSL_CLOSE_SOCKET(srv->server_fd);
		srv->server_fd = CDSL_INVALID_SOCKET;
		return -1;
	}

	if (CDSL_THREAD_CREATE(&srv->listener_thread, server_listener_routine, srv) != 0) {
		server_set_running(srv, false);
		CDSL_MUTEX_LOCK(&srv->queue_lock);
		CDSL_COND_BROADCAST(&srv->queue_not_empty);
		CDSL_MUTEX_UNLOCK(&srv->queue_lock);
		for (size_t i = 0; i < srv->num_workers; i++) {
			CDSL_THREAD_JOIN(srv->worker_threads[i]);
		}
		free(srv->worker_threads);
		srv->worker_threads = NULL;
		srv->num_workers = 0;
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

	/* Wait for listener thread to finish */
	CDSL_THREAD_JOIN(srv->listener_thread);

	if (srv->server_fd != CDSL_INVALID_SOCKET) {
		cdsl_socket_t fd = srv->server_fd;
		srv->server_fd = CDSL_INVALID_SOCKET;
		CDSL_CLOSE_SOCKET(fd);
	}

	/* Wake up all worker threads */
	CDSL_MUTEX_LOCK(&srv->queue_lock);
	CDSL_COND_BROADCAST(&srv->queue_not_empty);
	CDSL_COND_BROADCAST(&srv->queue_not_full);
	CDSL_MUTEX_UNLOCK(&srv->queue_lock);

	for (size_t i = 0; i < srv->num_workers; i++) {
		CDSL_THREAD_JOIN(srv->worker_threads[i]);
	}
	free(srv->worker_threads);
	srv->worker_threads = NULL;
	srv->num_workers = 0;

	/* Drain any remaining sockets in queue */
	CDSL_MUTEX_LOCK(&srv->queue_lock);
	while (srv->queue_count > 0) {
		cdsl_socket_t sock = srv->queue[srv->queue_head];
		srv->queue_head = (srv->queue_head + 1) % CDSL_SERVER_QUEUE_CAP;
		srv->queue_count--;
		if (sock != CDSL_INVALID_SOCKET) {
			CDSL_CLOSE_SOCKET(sock);
		}
	}
	CDSL_MUTEX_UNLOCK(&srv->queue_lock);
}
