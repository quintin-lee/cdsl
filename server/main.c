/**
 * @file src/server/main.c
 * @brief Standalone CLI executable for cdsl-server daemon.
 */

#include "cdsl/server.h"
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define sleep(x) Sleep((x) * 1000)
#else
#include <unistd.h>
#endif

static volatile int s_keep_running = 1;

static void
handle_sigint(int sig)
{
	(void)sig;
	s_keep_running = 0;
}

int
main(int argc, char** argv)
{
	int port = 8080;
	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
			port = atoi(argv[++i]);
		} else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			printf("Usage: cdsl-server [-p PORT]\n");
			return 0;
		}
	}

	signal(SIGINT, handle_sigint);
	signal(SIGTERM, handle_sigint);

	cdsl_server_config_t config = {
	    .port = port,
	    .max_concurrency = 4,
	    .verbose = true,
	};

	cdsl_server_t* srv = cdsl_server_create(&config);
	if (!srv) {
		fprintf(stderr, "Failed to create CDSL server\n");
		return 1;
	}

	if (cdsl_server_start(srv) != 0) {
		fprintf(stderr, "Failed to start CDSL server on port %d\n", port);
		cdsl_server_free(srv);
		return 1;
	}

	printf("CDSL cloud-native daemon listening on port %d...\n", cdsl_server_get_port(srv));

	while (s_keep_running) {
		sleep(1);
	}

	printf("Stopping CDSL server...\n");
	cdsl_server_stop(srv);
	cdsl_server_free(srv);
	printf("CDSL server exited cleanly.\n");

	return 0;
}
