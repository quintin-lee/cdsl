/**
 * @file src/ai/bridge_curl.c
 * @brief libcurl response collectors for the AI bridge (split from bridge.c).
 *
 * Concentrates everything that touches libcurl: the non-streaming write
 * callback that accumulates a full response body, and the streaming write
 * callback that buffers SSE lines.  Keeping these in one translation unit
 * means libcurl's header is only pulled in here, and the bridge_core /
 * registry sources stay curl-free.
 */

#include "bridge_internal.h"

#include <curl/curl.h>
#include <stdlib.h>
#include <string.h>

#ifdef CDSL_USE_CURL

/* Memory buffer for CURL response */
struct curl_mem_buffer {
	char* data;
	size_t size;
};

/* Callback to collect CURL response data */
size_t
cdsl_curl_write_callback(void* contents, size_t size, size_t nmemb, void* userp)
{
	size_t realsize = size * nmemb;
	struct curl_mem_buffer* mem = (struct curl_mem_buffer*)userp;

	char* ptr = realloc(mem->data, mem->size + realsize + 1);
	if (!ptr) {
		return 0; /* out of memory */
	}

	mem->data = ptr;
	memcpy(&(mem->data[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->data[mem->size] = 0;

	return realsize;
}

/**
 * @brief CURL callback for streaming responses.
 *
 * Buffers partial lines and processes them as SSE "data: " chunks.
 */
size_t
cdsl_curl_stream_callback(void* contents, size_t size, size_t nmemb, void* userp)
{
	size_t realsize = size * nmemb;
	struct curl_stream_ctx* ctx = (struct curl_stream_ctx*)userp;
	const char* p = (const char*)contents;

	for (size_t i = 0; i < realsize; i++) {
		if (p[i] == '\n') {
			ctx->line_buf[ctx->line_len] = '\0';
			if (strncmp(ctx->line_buf, "data: ", 6) == 0) {
				cdsl_ai_process_sse_data(ctx->line_buf + 6,
						ctx->result_ptr,
						ctx->total_ptr,
						ctx->cap_ptr,
						ctx->callback,
						ctx->user_data);
			}
			ctx->line_len = 0;
		} else if (ctx->line_len < sizeof(ctx->line_buf) - 1) {
			ctx->line_buf[ctx->line_len++] = p[i];
		}
	}
	return realsize;
}

#endif /* CDSL_USE_CURL */
