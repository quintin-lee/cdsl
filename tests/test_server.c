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

	/* 8. POST /v1/rules/deploy */
	char* resp8 = cdsl_server_handle_request(srv, "POST /v1/rules/deploy HTTP/1.1\r\n\r\n{}");
	assert(resp8 != NULL);
	assert(strstr(resp8, "200 OK") != NULL);
	assert(strstr(resp8, "\"status\":\"deployed\"") != NULL);
	free(resp8);

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
	int n = recv(sock, buf, sizeof(buf) - 1, 0);
	assert(n > 0);
	assert(strstr(buf, "200 OK") != NULL);
	assert(strstr(buf, "\"status\":\"healthy\"") != NULL);

	CDSL_CLOSE_SOCKET(sock);

	cdsl_server_stop(srv);
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
#ifdef _WIN32
	WSACleanup();
#endif
	printf("All server tests passed successfully!\n");
	return 0;
}
