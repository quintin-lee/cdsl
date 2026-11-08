/**
 * @file server.h
 * @brief Cloud-native HTTP micro-daemon for CDSL evaluation and metrics.
 *
 * Provides RESTful JSON endpoints (/v1/evaluate, /v1/rules/deploy),
 * Kubernetes probes (/healthz, /readyz), and Prometheus metrics (/metrics).
 */

#ifndef CDSL_SERVER_H
#define CDSL_SERVER_H

#include "cdsl/util/portability.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Server configuration options.
 */
typedef struct cdsl_server_config {
	int port;		/**< TCP port to bind (default: 8080) */
	size_t max_concurrency; /**< Worker thread pool size (default: 4) */
	bool verbose;		/**< Enable access logging */
} cdsl_server_config_t;

/**
 * @brief Opaque server instance handle.
 */
typedef struct cdsl_server cdsl_server_t;

/**
 * @brief Create a new HTTP daemon instance.
 * @param config Server configuration (or NULL for default settings)
 * @return Allocated server handle, or NULL on failure
 */
CDSL_NODISCARD
cdsl_server_t* cdsl_server_create(const cdsl_server_config_t* config);

/**
 * @brief Start the server (non-blocking in a background thread).
 * @param srv Server instance
 * @return 0 on success, negative on error
 */
int cdsl_server_start(cdsl_server_t* srv);

/**
 * @brief Stop the running server gracefully.
 * @param srv Server instance
 */
void cdsl_server_stop(cdsl_server_t* srv);

/**
 * @brief Free the server instance and associated resources.
 * @param srv Server instance (NULL-safe)
 */
void cdsl_server_free(cdsl_server_t* srv);

/**
 * @brief Retrieve the actual bound port (useful when port 0 is passed).
 * @param srv Server instance
 * @return Bound TCP port, or 0 if not running
 */
int cdsl_server_get_port(const cdsl_server_t* srv);

/**
 * @brief Directly dispatch an HTTP request string and return the HTTP response string.
 * @param srv Server instance
 * @param http_request Raw HTTP request text
 * @return Heap-allocated HTTP response text (free with free())
 */
CDSL_NODISCARD
char* cdsl_server_handle_request(cdsl_server_t* srv, const char* http_request);

#ifdef __cplusplus
}
#endif

#endif /* CDSL_SERVER_H */
