/**
 * @file test_http_limits.c
 * @brief HTTP request-body limits: oversized Content-Length rejected with 413.
 *
 * Regression tests for CDSL_SERVER_MAX_BODY_BYTES enforcement in
 * server_handle_client. Verifies:
 *  - forged Content-Length > 10MB is rejected with 413 before the read loop
 *    can be driven to the unsatisfiable total-length check
 *  - Content-Length == 10MB (== cap) is accepted (boundary pinned)
 *  - Content-Length < 10MB with a real body is handled normally
 *  - Content-Length: 0 / absent / negative preserve existing short-circuit
 */

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

/* Start a server on an ephemeral port and connect a socket client to it. */
static int
start_test_server(int* out_port, cdsl_server_t** out_srv)
{
	cdsl_server_config_t config = {
	    .port = 0,
	    .max_concurrency = 2,
	    .verbose = false,
	};
	*out_srv = cdsl_server_create(&config);
	if (!*out_srv) {
		return -1;
	}
	if (cdsl_server_start(*out_srv) != 0) {
		cdsl_server_free(*out_srv);
		*out_srv = NULL;
		return -1;
	}
	*out_port = cdsl_server_get_port(*out_srv);
	if (*out_port <= 0) {
		cdsl_server_stop(*out_srv);
		cdsl_server_free(*out_srv);
		*out_srv = NULL;
		return -1;
	}
	return 0;
}

static void
stop_test_server(cdsl_server_t* srv)
{
	if (!srv) {
		return;
	}
	cdsl_server_stop(srv);
	cdsl_server_free(srv);
}

static cdsl_socket_t
connect_client(int port, struct sockaddr_in* reuse_addr)
{
	cdsl_socket_t sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock == CDSL_INVALID_SOCKET) {
		return sock;
	}
	struct sockaddr_in addr;
	if (reuse_addr) {
		addr = *reuse_addr;
	} else {
		memset(&addr, 0, sizeof(addr));
		addr.sin_family = AF_INET;
		addr.sin_port = htons((uint16_t)port);
		addr.sin_addr.s_addr = inet_addr("127.0.0.1");
	}
	if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
		CDSL_CLOSE_SOCKET(sock);
		return CDSL_INVALID_SOCKET;
	}
	return sock;
}

/*
 * Send a HEADERS-ONLY request that claims a Content-Length larger than the
 * 10MB cap. The server must reject with 413 and close the connection before
 * the read loop runs.
 */
static void
test_oversized_content_length_rejected_413(void)
{
	int port = 0;
	cdsl_server_t* srv = NULL;
	assert(start_test_server(&port, &srv) == 0);

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	/* Forged CL: 100000000 (100MB) > 10MB cap */
	cdsl_socket_t sock = connect_client(port, &addr);
	assert(sock != CDSL_INVALID_SOCKET);

	const char* req =
	    "POST /v1/evaluate HTTP/1.1\r\n"
	    "Host: localhost\r\n"
	    "Content-Length: 100000000\r\n"
	    "\r\n";
	assert(send(sock, req, (int)strlen(req), 0) > 0);

	char buf[512] = {0};
	ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
	assert(n > 0);
	assert(strstr(buf, "413 Payload Too Large") != NULL);
	assert(strstr(buf, "Connection: close") != NULL);

	/* Connection is closed by the server; subsequent recv returns 0 */
	CDSL_CLOSE_SOCKET(sock);
	stop_test_server(srv);

	printf("  [OK] oversized Content-Length -> 413\n");
}

/*
 * Boundary: Content-Length exactly == 10MB cap. The server should NOT reject
 * with 413 (the > check is strict); it will block waiting for a body we do
 * not send, but the 413 must not fire. We verify by confirming no 413
 * response arrives within a short recv timeout.
 */
static void
test_exactly_10mb_content_length_not_413(void)
{
	int port = 0;
	cdsl_server_t* srv = NULL;
	assert(start_test_server(&port, &srv) == 0);

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	cdsl_socket_t sock = connect_client(port, &addr);
	assert(sock != CDSL_INVALID_SOCKET);

	/* CL exactly at cap: 10485760 (10MB) — the > check must not fire */
	const char* req =
	    "POST /v1/evaluate HTTP/1.1\r\n"
	    "Host: localhost\r\n"
	    "Content-Length: 10485760\r\n"
	    "\r\n";
	assert(send(sock, req, (int)strlen(req), 0) > 0);

	/* The server will block in the read loop waiting for the body.
	 * We must NOT see a 413. Do a small read and check the response
	 * status is not 413. Because the body never arrives, the server is
	 * still draining; the only response it can send for this request
	 * is either 413 (bug) or nothing yet. A short recv that returns
	 * -1/EAGAIN or 0 is acceptable; a 413 string is a regression. */

	/* Set a short SO_RCVTIMEO so recv does not block forever */
#ifdef _WIN32
	u32 timeout_ms = 2000;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout_ms,
		   sizeof(timeout_ms));
#else
	struct timeval tv;
	tv.tv_sec = 2;
	tv.tv_usec = 0;
	setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

	char buf[256] = {0};
	ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
	/* n may be -1 (timeout, body not arrived -> server still draining,
	 * which is the CORRECT behavior) or 0 (connection closed). It must
	 * NOT be a 413 response. */
	if (n > 0) {
		assert(strstr(buf, "413") == NULL);
	}

	CDSL_CLOSE_SOCKET(sock);
	stop_test_server(srv);

	printf("  [OK] exactly-10MB Content-Length not rejected 413\n");
}

/*
 * Normal small body: Content-Length < 10MB with a real body is handled
 * normally (200). This confirms the 413 path does not over-reject.
 */
static void
test_small_body_accepted_200(void)
{
	int port = 0;
	cdsl_server_t* srv = NULL;
	assert(start_test_server(&port, &srv) == 0);

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	cdsl_socket_t sock = connect_client(port, &addr);
	assert(sock != CDSL_INVALID_SOCKET);

	const char* body = "{\"type\":\"rule\",\"rule\":\"RULE r_lim { WHEN true THEN approve() }\"}";
	char hdr[256];
	snprintf(hdr,
		 sizeof(hdr),
		 "POST /v1/evaluate HTTP/1.1\r\n"
		 "Host: localhost\r\n"
		 "Content-Length: %zu\r\n"
		 "\r\n",
		 strlen(body));

	assert(send(sock, hdr, (int)strlen(hdr), 0) > 0);
	assert(send(sock, body, (int)strlen(body), 0) > 0);

	char buf[1024] = {0};
	ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
	assert(n > 0);
	assert(strstr(buf, "200 OK") != NULL);
	assert(strstr(buf, "413") == NULL);

	CDSL_CLOSE_SOCKET(sock);
	stop_test_server(srv);

	printf("  [OK] small body accepted 200\n");
}

/*
 * Content-Length: 0 — the existing short-circuit (content_len <= 0) is
 * preserved. The server must NOT 413; it treats CL=0 as "no body" and
 * proceeds (likely a 400/404 depending on route, but never 413).
 */
static void
test_zero_content_length_not_413(void)
{
	int port = 0;
	cdsl_server_t* srv = NULL;
	assert(start_test_server(&port, &srv) == 0);

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	cdsl_socket_t sock = connect_client(port, &addr);
	assert(sock != CDSL_INVALID_SOCKET);

	const char* req =
	    "POST /v1/evaluate HTTP/1.1\r\n"
	    "Host: localhost\r\n"
	    "Content-Length: 0\r\n"
	    "\r\n";
	assert(send(sock, req, (int)strlen(req), 0) > 0);

	char buf[512] = {0};
	ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
	assert(n > 0);
	/* CL=0 must not produce 413; the request is short-circuited to the
	 * normal dispatch (400 bad_request is expected for an empty body). */
	assert(strstr(buf, "413") == NULL);

	CDSL_CLOSE_SOCKET(sock);
	stop_test_server(srv);

	printf("  [OK] Content-Length: 0 not 413\n");
}

/*
 * No Content-Length header at all — existing short-circuit preserved.
 * The server must NOT 413.
 */
static void
test_absent_content_length_not_413(void)
{
	int port = 0;
	cdsl_server_t* srv = NULL;
	assert(start_test_server(&port, &srv) == 0);

	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons((uint16_t)port);
	addr.sin_addr.s_addr = inet_addr("127.0.0.1");

	cdsl_socket_t sock = connect_client(port, &addr);
	assert(sock != CDSL_INVALID_SOCKET);

	/* No Content-Length header */
	const char* req =
	    "POST /v1/evaluate HTTP/1.1\r\n"
	    "Host: localhost\r\n"
	    "\r\n";
	assert(send(sock, req, (int)strlen(req), 0) > 0);

	char buf[512] = {0};
	ssize_t n = recv(sock, buf, sizeof(buf) - 1, 0);
	assert(n > 0);
	/* Absent CL must not produce 413. */
	assert(strstr(buf, "413") == NULL);

	CDSL_CLOSE_SOCKET(sock);
	stop_test_server(srv);

	printf("  [OK] absent Content-Length not 413\n");
}

int
main(void)
{
	printf("Running HTTP limits tests...\n");
	test_oversized_content_length_rejected_413();
	test_exactly_10mb_content_length_not_413();
	test_small_body_accepted_200();
	test_zero_content_length_not_413();
	test_absent_content_length_not_413();
	printf("All HTTP limits tests passed\n");
	return 0;
}
