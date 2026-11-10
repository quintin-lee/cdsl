/**
 * @file src/ai/bridge_internal.h
 * @brief Internal shared declarations for the AI bridge split.
 *
 * The AI module is compiled as three translation units (bridge.c,
 * bridge_curl.c, bridge_registry.c).  This header declares the handful of
 * symbols they share so no implementation detail needs to live in the public
 * API.  Internal header — do not include outside src/ai/.
 *
 * @defgroup ai_bridge_internal AI Bridge Internal Declarations
 * @{
 */

#ifndef CDSL_BRIDGE_INTERNAL_H
#define CDSL_BRIDGE_INTERNAL_H

#include "cdsl/ai.h"
#include "cdsl/util/hashmap.h"
#include "cdsl/util/portability.h"
#include "cdsl/util/threads.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Context for CURL streaming (internal, shared with bridge_curl.c).
 */
struct curl_mem_buffer {
	char* data;
	size_t size;
};

struct curl_stream_ctx {
	cdsl_ai_stream_cb_t callback; /**< User callback */
	void* user_data;	      /**< User opaque data */
	char** result_ptr;	      /**< Pointer to accumulated result */
	size_t* total_ptr;	      /**< Pointer to total length */
	size_t* cap_ptr;	      /**< Pointer to capacity */
	char line_buf[16384];	      /**< Internal line buffer for SSE */
	size_t line_len;	      /**< Current line buffer length */
};

/* ---- shared helpers (defined in bridge.c) -------------------------------- */

/**
 * @brief Process a single SSE "data: " JSON chunk, appending content to the
 *        accumulated result and invoking the stream callback.
 *
 * Used by the curl streaming callback and the popen() fallback.
 */
void cdsl_ai_process_sse_data(const char* data,
			      char** result_ptr,
			      size_t* total_ptr,
			      size_t* cap_ptr,
			      cdsl_ai_stream_cb_t callback,
			      void* user_data);

/* ---- registry lookups (defined in bridge_registry.c) --------------------- */

/**
 * @brief Look up a registered AI provider by name.
 * @return Registered provider, or NULL if none was registered under that
 *         name.  Callers fall back to the built-in default provider.
 */
cdsl_ai_provider_t* cdsl_ai_get_provider(const char* name);

/**
 * @brief Resolve the cache driver for a config.
 *
 * Prefers an inline cfg->cache; otherwise looks up cfg->cache_driver_name in
 * the global cache-driver registry.
 *
 * @return Cache driver, or NULL if none applies.
 */
cdsl_ai_cache_t* cdsl_ai_get_cache_driver(const cdsl_ai_config_t* cfg);

/* ---- libcurl callbacks (defined in bridge_curl.c) ------------------------- */

#ifdef CDSL_USE_CURL
/**
 * @brief libcurl write callback that accumulates a full response body.
 */
size_t cdsl_curl_write_callback(void* contents, size_t size, size_t nmemb, void* userp);

/**
 * @brief libcurl write callback that buffers SSE lines for streaming.
 */
size_t cdsl_curl_stream_callback(void* contents, size_t size, size_t nmemb, void* userp);
#endif

#ifdef __cplusplus
}
#endif

#endif /* CDSL_BRIDGE_INTERNAL_H */
/** @} */
