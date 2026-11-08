#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>

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
typedef int cdsl_socket_t;
#define CDSL_INVALID_SOCKET (-1)
#define CDSL_CLOSE_SOCKET(s) close(s)
#endif

#include "cdsl/server.h"
#include "cdsl/util/threads.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
test_server_direct_dispatch(void)
{
	cdsl_server_t* srv = cdsl_server_create(NULL);
	assert(srv != NULL);

	/* 1. /healthz */
	char* resp1 = cdsl_server_handle_request(srv, "GET /healthz HTTP/1.1\r\n\r\n");
	assert(resp1 != NULL);
	assert(strstr(resp1, "200 OK") != NULL);
	assert(strstr(resp1, "\"status\":\"healthy\"") != NULL);
	free(resp1);

	/* 2. /readyz */
	char* resp2 = cdsl_server_handle_request(srv, "GET /readyz HTTP/1.1\r\n\r\n");
	assert(resp2 != NULL);
	assert(strstr(resp2, "200 OK") != NULL);
	assert(strstr(resp2, "\"status\":\"ready\"") != NULL);
	free(resp2);

	/* 3. /metrics */
	char* resp3 = cdsl_server_handle_request(srv, "GET /metrics HTTP/1.1\r\n\r\n");
	assert(resp3 != NULL);
	assert(strstr(resp3, "200 OK") != NULL);
	assert(strstr(resp3, "cdsl_http_requests_total") != NULL);
	free(resp3);

	/* 4. POST /v1/evaluate (Rule) */
	const char* eval_req = "POST /v1/evaluate HTTP/1.1\r\n"
			       "Content-Type: application/json\r\n\r\n"
			       "RULE check_vip { WHEN is_vip == true THEN approve() }";
	char* resp4 = cdsl_server_handle_request(srv, eval_req);
	assert(resp4 != NULL);
	assert(strstr(resp4, "200 OK") != NULL);
	free(resp4);

	/* 5. POST /v1/evaluate (Decision Table) */
	const char* dt_req = "POST /v1/evaluate HTTP/1.1\r\n\r\n"
			     "DECISION_TABLE t1\nINPUTS (x: INT)\nOUTPUTS (y: INT)\nHIT_POLICY: "
			     "FIRST\nROW: _ -> 1\nEND\n";
	char* resp5 = cdsl_server_handle_request(srv, dt_req);
	assert(resp5 != NULL);
	assert(strstr(resp5, "200 OK") != NULL);
	assert(strstr(resp5, "\"type\":\"dtable\"") != NULL);
	free(resp5);

	/* 6. POST /v1/evaluate (Scorecard) */
	const char* sc_req = "POST /v1/evaluate HTTP/1.1\r\n\r\n"
			     "SCORECARD sc1\nBASELINE: 500\nEND\n";
	char* resp6 = cdsl_server_handle_request(srv, sc_req);
	assert(resp6 != NULL);
	assert(strstr(resp6, "200 OK") != NULL);
	assert(strstr(resp6, "\"type\":\"scorecard\"") != NULL);
	free(resp6);

	/* 7. POST /v1/evaluate (Pipeline) */
	const char* pipe_req = "POST /v1/evaluate HTTP/1.1\r\n\r\n"
			       "PIPELINE p1\nSTAGE s1:\nEND\n";
	char* resp7 = cdsl_server_handle_request(srv, pipe_req);
	assert(resp7 != NULL);
	assert(strstr(resp7, "200 OK") != NULL);
	assert(strstr(resp7, "\"type\":\"pipeline\"") != NULL);
	free(resp7);

	/* 8. POST /v1/rules/deploy with empty body should be rejected with 400 */
	char* resp8 = cdsl_server_handle_request(srv, "POST /v1/rules/deploy HTTP/1.1\r\n\r\n{}");
	assert(resp8 != NULL);
	assert(strstr(resp8, "400 Bad Request") != NULL);
	assert(strstr(resp8, "\"status\":\"rejected\"") != NULL);
	free(resp8);

	/* 8b. POST /v1/rules/deploy with valid rule and tag */
	const char* deploy_req =
	    "POST /v1/rules/deploy HTTP/1.1\r\n"
	    "Content-Type: application/json\r\n\r\n"
	    "{\"rule\":\"RULE can1 { WHEN true THEN approve() }\",\"tag\":\"v1.0\"}";
	char* resp8b = cdsl_server_handle_request(srv, deploy_req);
	assert(resp8b != NULL);
	assert(strstr(resp8b, "200 OK") != NULL);
	assert(strstr(resp8b, "\"status\":\"deployed\"") != NULL);
	assert(strstr(resp8b, "\"version\":") != NULL);
	free(resp8b);

	/* 8c. POST /v1/rules/deploy with invalid syntax */
	const char* bad_deploy = "POST /v1/rules/deploy HTTP/1.1\r\n\r\n"
				 "{\"rule\":\"INVALID SYNTAX !!!\"}";
	char* resp8c = cdsl_server_handle_request(srv, bad_deploy);
	assert(resp8c != NULL);
	assert(strstr(resp8c, "400 Bad Request") != NULL);
	assert(strstr(resp8c, "\"status\":\"rejected\"") != NULL);
	free(resp8c);

	/* 8d. Negative test: POST /v1/evaluate with empty payload when no rule provided */
	cdsl_server_t* srv_fresh = cdsl_server_create(NULL);
	char* resp_empty =
	    cdsl_server_handle_request(srv_fresh, "POST /v1/evaluate HTTP/1.1\r\n\r\n");
	assert(resp_empty != NULL);
	assert(strstr(resp_empty, "400 Bad Request") != NULL);
	assert(strstr(resp_empty, "\"bad_request\"") != NULL);
	free(resp_empty);

	/* 8e. Negative test: POST /v1/evaluate with invalid syntax */
	const char* malformed_eval = "POST /v1/evaluate HTTP/1.1\r\n\r\n"
				     "RULE bad { WHEN >>> 100 }";
	char* resp_malformed = cdsl_server_handle_request(srv_fresh, malformed_eval);
	assert(resp_malformed != NULL);
	assert(strstr(resp_malformed, "400 Bad Request") != NULL);
	free(resp_malformed);
	cdsl_server_free(srv_fresh);

	/* 8d. JSON field-based routing for /v1/evaluate */
	const char* json_eval_rule =
	    "POST /v1/evaluate HTTP/1.1\r\n\r\n"
	    "{\"type\":\"rule\",\"rule\":\"RULE r_json { WHEN score > 80 THEN pass() "
	    "}\",\"context\":{\"score\":90}}";
	char* resp8d = cdsl_server_handle_request(srv, json_eval_rule);
	assert(resp8d != NULL);
	assert(strstr(resp8d, "200 OK") != NULL);
	free(resp8d);

	const char* json_eval_dtable =
	    "POST /v1/evaluate HTTP/1.1\r\n\r\n"
	    "{\"type\":\"dtable\",\"dtable\":\"DECISION_TABLE dt_json\\nINPUTS (x: "
	    "INT)\\nOUTPUTS (y: INT)\\nHIT_POLICY: FIRST\\nROW: _ -> 10\\nEND\\n\"}";
	char* resp8e = cdsl_server_handle_request(srv, json_eval_dtable);
	assert(resp8e != NULL);
	assert(strstr(resp8e, "200 OK") != NULL);
	assert(strstr(resp8e, "\"type\":\"dtable\"") != NULL);
	free(resp8e);

	/* 9. 404 */
	char* resp9 = cdsl_server_handle_request(srv, "GET /unknown HTTP/1.1\r\n\r\n");
	assert(resp9 != NULL);
	assert(strstr(resp9, "404 Not Found") != NULL);
	free(resp9);

	cdsl_server_free(srv);
}

static void
test_server_socket_networking(void)
{
	cdsl_server_config_t config = {
	    .port = 0, /* Ephemeral port */
	    .max_concurrency = 2,
	    .verbose = false,
	};
	cdsl_server_t* srv = cdsl_server_create(&config);
	assert(srv != NULL);
	assert(cdsl_server_start(srv) == 0);

	int port = cdsl_server_get_port(srv);
	assert(port > 0);

	/* Connect via socket client */
	cdsl_socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
	assert(sock != CDSL_INVALID_SOCKET);

	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons((uint16_t)port);
	serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	assert(connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == 0);

	const char* http_req = "GET /healthz HTTP/1.1\r\nHost: localhost\r\n\r\n";
	assert(send(sock, http_req, (int)strlen(http_req), 0) > 0);

	char buf[1024] = {0};
	ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
	assert(n > 0);
	assert(strstr(buf, "200 OK") != NULL);
	assert(strstr(buf, "\"status\":\"healthy\"") != NULL);

	CDSL_CLOSE_SOCKET(sock);

	/* Connect again and send POST with Content-Length split across two chunks */
	cdsl_socket_t sock2 = socket(AF_INET, SOCK_STREAM, 0);
	assert(sock2 != CDSL_INVALID_SOCKET);
	assert(connect(sock2, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) == 0);

	const char* body =
	    "{\"type\":\"rule\",\"rule\":\"RULE r_net { WHEN true THEN approve() }\"}";
	char hdr[256];
	snprintf(hdr,
		 sizeof(hdr),
		 "POST /v1/evaluate HTTP/1.1\r\nHost: localhost\r\nContent-Length: "
		 "%zu\r\n\r\n",
		 strlen(body));

	/* Send header first */
	assert(send(sock2, hdr, (int)strlen(hdr), 0) > 0);
	/* Send body second */
	assert(send(sock2, body, (int)strlen(body), 0) > 0);

	memset(buf, 0, sizeof(buf));
	n = recv(sock2, buf, sizeof(buf) - 1, 0);
	assert(n > 0);
	assert(strstr(buf, "200 OK") != NULL);
	assert(strstr(buf, "\"status\":") != NULL);
	assert(strstr(buf, "r_net") != NULL);
	CDSL_CLOSE_SOCKET(sock2);

	cdsl_server_stop(srv);
	cdsl_server_free(srv);
}

typedef struct client_thread_arg {
	int port;
	int count;
	int success_count;
} client_thread_arg_t;

static void*
client_worker(void* arg)
{
	client_thread_arg_t* c = (client_thread_arg_t*)arg;
	for (int i = 0; i < c->count; i++) {
		cdsl_socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock == CDSL_INVALID_SOCKET) {
			continue;
		}
		struct sockaddr_in serv_addr;
		memset(&serv_addr, 0, sizeof(serv_addr));
		serv_addr.sin_family = AF_INET;
		serv_addr.sin_port = htons((uint16_t)c->port);
		serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");

		if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) != 0) {
			CDSL_CLOSE_SOCKET(sock);
			continue;
		}

		const char* req =
		    (i % 2 == 0)
			? "GET /healthz HTTP/1.1\r\nHost: localhost\r\n\r\n"
			: "POST /v1/evaluate HTTP/1.1\r\nHost: localhost\r\nContent-Length: "
			  "60\r\n\r\n"
			  "{\"type\":\"rule\",\"rule\":\"RULE conc { WHEN true THEN approve() "
			  "}\"}";

		if (send(sock, req, (int)strlen(req), 0) > 0) {
			char resp[512] = {0};
			ssize_t n = recv(sock, resp, sizeof(resp) - 1, 0);
			if (n > 0 && strstr(resp, "200 OK") != NULL) {
				c->success_count++;
			}
		}
		CDSL_CLOSE_SOCKET(sock);
	}
	return NULL;
}

static void
test_server_concurrency(void)
{
	cdsl_server_config_t config = {
	    .port = 0,
	    .max_concurrency = 4,
	    .verbose = false,
	};
	cdsl_server_t* srv = cdsl_server_create(&config);
	assert(srv != NULL);
	assert(cdsl_server_start(srv) == 0);

	int port = cdsl_server_get_port(srv);
	assert(port > 0);

	const int num_threads = 6;
	const int reqs_per_thread = 8;
	cdsl_thread_t threads[6];
	client_thread_arg_t args[6];

	for (int i = 0; i < num_threads; i++) {
		args[i].port = port;
		args[i].count = reqs_per_thread;
		args[i].success_count = 0;
		assert(CDSL_THREAD_CREATE(&threads[i], client_worker, &args[i]) == 0);
	}

	int total_success = 0;
	for (int i = 0; i < num_threads; i++) {
		CDSL_THREAD_JOIN(threads[i]);
		total_success += args[i].success_count;
	}

	assert(total_success == num_threads * reqs_per_thread);

	cdsl_server_stop(srv);
	cdsl_server_free(srv);
}

static void
test_server_dynamic_large_response(void)
{
	cdsl_server_t* srv = cdsl_server_create(NULL);
	assert(srv != NULL);

	const char* large_rule = "POST /v1/evaluate HTTP/1.1\r\n\r\n"
				 "RULE large_check {\n"
				 "  WHEN score > 50\n"
				 "  THEN approve()\n"
				 "}";
	char* resp = cdsl_server_handle_request(srv, large_rule);
	assert(resp != NULL);
	assert(strstr(resp, "200 OK") != NULL);
	assert(strstr(resp, "Content-Length:") != NULL);
	free(resp);

	cdsl_server_free(srv);
}

int
main(void)
{
#ifdef _WIN32
	WSADATA wsa;
	(void)WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
	test_server_direct_dispatch();
	test_server_socket_networking();
	test_server_concurrency();
	test_server_dynamic_large_response();
#ifdef _WIN32
	WSACleanup();
#endif
	printf("All server tests passed successfully!\n");
	return 0;
}
