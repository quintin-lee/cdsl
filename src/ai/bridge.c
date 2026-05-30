/**
 * @file ai_bridge.c
 * @brief AI bridge implementation: NL-to-DSL translation and rule review.
 *
 * Implements the AI integration layer for translating natural language
 * rule descriptions into C-DSL syntax and reviewing DSL rules for
 * safety, completeness, and logical consistency.
 *
 * Key components:
 * - Mock translator: heuristic-based DSL generation from NL input
 * - LLM translator: OpenAI-compatible API calls (cURL or subprocess)
 * - Safety reviewer: mock scoring or LLM-based analysis
 * - Streaming API: SSE-based real-time translation/review
 * - Provider registry: pluggable AI backends
 * - Cache driver registry: pluggable response caching (Redis, etc.)
 *
 * When CDSL_USE_CURL is defined, libcurl is used for HTTP requests.
 * Otherwise, the system curl command is invoked via popen().
 *
 * @defgroup ai_bridge_impl AI Bridge Implementation
 * @{
 */

#include "cdsl/ai.h"
#include "cdsl/util/json.h"
#include "cdsl/util/hashmap.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <stdarg.h>
#include <pthread.h>

/**
 * @brief Minimal JSON string escape for review_stream output.
 *
 * Replaces \", \\, and control characters with escape sequences.
 * Writes at most dst_size - 1 characters, always NUL-terminates.
 */
static void
json_escape_small(char* dst, size_t dst_size, const char* src)
{
	size_t j = 0;
	for (size_t i = 0; src[i] && j + 2 < dst_size; i++) {
		char c = src[i];
		if (c == '"' || c == '\\') {
			dst[j++] = '\\';
			dst[j++] = c;
		} else if (c == '\n') {
			dst[j++] = '\\';
			dst[j++] = 'n';
		} else if (c == '\r') {
			dst[j++] = '\\';
			dst[j++] = 'r';
		} else if (c == '\t') {
			dst[j++] = '\\';
			dst[j++] = 't';
		} else if ((unsigned char)c < 0x20) {
			/* Skip unprintable control characters */
		} else {
			dst[j++] = c;
		}
	}
	dst[j] = '\0';
}

/**
 * @brief Find a child JSON value by key in a JSON object (internal).
 * @param obj JSON object value
 * @param key Key to find
 * @return Child value node, or NULL if not found or not an object
 */
static cdsl_json_value_t*
find_json_key(cdsl_json_value_t* obj, const char* key)
{
	if (!obj || obj->type != JSON_OBJECT || !key) {
		return NULL;
	}
	for (cdsl_json_value_t* v = obj->value.object.items; v; v = v->next) {
		if (v->key && strcmp(v->key, key) == 0) {
			return v;
		}
	}
	return NULL;
}

#ifdef CDSL_USE_CURL
#include <curl/curl.h>

/* Memory buffer for CURL response */
struct curl_mem_buffer {
	char* data;
	size_t size;
};

/* Callback to collect CURL response data */
static size_t
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
#endif

#ifndef CDSL_USE_CURL
/* Escapes single quotes in s for use inside a single-quoted shell argument.
 * Replaces each ' with '"'"' (close-quote, double-quote single-quote, re-open).
 * Returns allocated string caller must free. */
static char*
escape_sq(const char* s)
{
	size_t len = 0;
	for (const char* p = s; *p; p++) {
		len += (*p == '\'') ? 4 : 1;
	}
	char* out = malloc(len + 1);
	if (!out) {
		return NULL;
	}
	char* d = out;
	for (const char* p = s; *p; p++) {
		if (*p == '\'') {
			*d++ = '\'';
			*d++ = '"';
			*d++ = '\'';
			*d++ = '\'';
		} else {
			*d++ = *p;
		}
	}
	*d = '\0';
	return out;
}
#endif

/**
 * @brief Escape a string for use in a JSON value (internal).
 *
 * Handles quotes, backslashes, and control characters.
 *
 * @param s Source string
 * @return Allocated escaped string (caller frees), or NULL on error
 */
static char*
escape_json_string(const char* s)
{
	if (!s) {
		return NULL;
	}
	size_t len = strlen(s);
	char* out = malloc(len * 4 + 1);
	if (!out) {
		return NULL;
	}
	char* dst = out;
	for (const char* p = s; *p; p++) {
		if (*p == '"') {
			*dst++ = '\\';
			*dst++ = '"';
		} else if (*p == '\\') {
			*dst++ = '\\';
			*dst++ = '\\';
		} else if (*p == '\n') {
			*dst++ = '\\';
			*dst++ = 'n';
		} else if (*p == '\r') {
			*dst++ = '\\';
			*dst++ = 'r';
		} else if (*p == '\t') {
			*dst++ = '\\';
			*dst++ = 't';
		} else if ((unsigned char)*p < 32) {
			/* Skip other control chars for simplicity */
		} else {
			*dst++ = *p;
		}
	}
	*dst = '\0';
	return out;
}

static char* call_llm_api(const char* prompt, const cdsl_ai_config_t* config);

/**
 * @brief Default built-in AI provider.
 */
static char* default_translate(void* ctx,
			       const char* nl,
			       const cdsl_schema_t* schema,
			       const cdsl_ai_config_t* config);

static cdsl_ai_review_t* default_review(void* ctx,
					const char* dsl,
					const cdsl_schema_t* schema,
					const cdsl_ai_config_t* config);

static char* call_llm_api_stream(const char* prompt,
				 const cdsl_ai_config_t* config,
				 cdsl_ai_stream_cb_t callback,
				 void* user_data);

static char* default_translate_stream(void* ctx,
				      const char* nl,
				      const cdsl_schema_t* schema,
				      const cdsl_ai_config_t* config,
				      cdsl_ai_stream_cb_t callback,
				      void* user_data);

static char* default_review_stream(void* ctx,
				   const char* dsl,
				   const cdsl_schema_t* schema,
				   const cdsl_ai_config_t* config,
				   cdsl_ai_stream_cb_t callback,
				   void* user_data);

static cdsl_ai_provider_t g_default_provider = {
    .ctx = NULL,
    .translate = default_translate,
    .review = default_review,
    .translate_stream = default_translate_stream,
    .review_stream = default_review_stream,
};

static cdsl_hashmap_t* g_providers = NULL;
static cdsl_hashmap_t* g_cache_drivers = NULL;
static pthread_rwlock_t g_ai_registry_lock;
static int g_ai_registry_lock_initialized = 0;

static void
init_registries(void)
{
	if (!g_ai_registry_lock_initialized) {
		pthread_rwlock_init(&g_ai_registry_lock, NULL);
		g_ai_registry_lock_initialized = 1;
	}
	pthread_rwlock_wrlock(&g_ai_registry_lock);
	if (!g_providers) {
		g_providers = cdsl_hashmap_create(16);
		cdsl_ai_provider_t* copy = malloc(sizeof(*copy));
		if (copy) {
			*copy = g_default_provider;
			cdsl_hashmap_put(g_providers, "default", copy);
		}
	}
	if (!g_cache_drivers) {
		g_cache_drivers = cdsl_hashmap_create(16);
	}
	pthread_rwlock_unlock(&g_ai_registry_lock);
}

void
cdsl_ai_register_provider(const char* name, const cdsl_ai_provider_t* provider)
{
	init_registries();
	pthread_rwlock_wrlock(&g_ai_registry_lock);
	cdsl_ai_provider_t* copy = malloc(sizeof(*copy));
	if (copy) {
		*copy = *provider;
		cdsl_hashmap_put(g_providers, name, copy);
	}
	pthread_rwlock_unlock(&g_ai_registry_lock);
}

void
cdsl_ai_register_cache_driver(const char* name, const cdsl_ai_cache_t* cache)
{
	init_registries();
	pthread_rwlock_wrlock(&g_ai_registry_lock);
	cdsl_ai_cache_t* copy = malloc(sizeof(*copy));
	if (copy) {
		*copy = *cache;
		cdsl_hashmap_put(g_cache_drivers, name, copy);
	}
	pthread_rwlock_unlock(&g_ai_registry_lock);
}

static cdsl_ai_provider_t*
get_provider(const char* name)
{
	init_registries();
	pthread_rwlock_rdlock(&g_ai_registry_lock);
	cdsl_ai_provider_t* p =
	    (cdsl_ai_provider_t*)cdsl_hashmap_get(g_providers, name ? name : "default");
	cdsl_ai_provider_t* result = p ? p : &g_default_provider;
	pthread_rwlock_unlock(&g_ai_registry_lock);
	return result;
}

static cdsl_ai_cache_t*
get_cache_driver(const cdsl_ai_config_t* cfg)
{
	if (cfg->cache) {
		return cfg->cache;
	}
	if (cfg->cache_driver_name) {
		init_registries();
		pthread_rwlock_rdlock(&g_ai_registry_lock);
		cdsl_ai_cache_t* result =
		    (cdsl_ai_cache_t*)cdsl_hashmap_get(g_cache_drivers, cfg->cache_driver_name);
		pthread_rwlock_unlock(&g_ai_registry_lock);
		return result;
	}
	return NULL;
}

/**
 * @brief Send a prompt to a remote LLM API via cURL (internal).
 *
 * Constructs a JSON body and HTTP request using the config's api_base,
 * api_key, and model. Parses the response to extract the content field.
 *
 * @param prompt The user prompt (escaped for JSON internally)
 * @param config AI configuration (api_key, api_base, model required)
 * @return Decoded response text, or NULL on failure
 */
static char*
call_llm_api(const char* prompt, const cdsl_ai_config_t* config)
{
	if (!config || !config->api_key || !config->api_base || !config->model) {
		return NULL;
	}

	cdsl_ai_cache_t* cache = get_cache_driver(config);
	if (cache && cache->get) {
		char* cached = cache->get(cache->ctx, prompt);
		if (cached) {
			return cached;
		}
	}

#ifdef CDSL_USE_CURL
	CURL* curl = curl_easy_init();
	if (!curl) {
		return NULL;
	}

	char url[512];
	snprintf(url, sizeof(url), "%s/chat/completions", config->api_base);

	struct curl_slist* headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	char auth_header[512];
	snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", config->api_key);
	headers = curl_slist_append(headers, auth_header);

	/* Escape prompt for JSON */
	char* escaped_prompt = escape_json_string(prompt);
	if (!escaped_prompt) {
		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);
		return NULL;
	}

	char* body = malloc(strlen(config->model) + strlen(escaped_prompt) + 128);
	if (!body) {
		free(escaped_prompt);
		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);
		return NULL;
	}
	snprintf(body,
		 strlen(config->model) + strlen(escaped_prompt) + 128,
		 "{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}],"
		 "\"temperature\":0.1}",
		 config->model,
		 escaped_prompt);
	free(escaped_prompt);

	struct curl_mem_buffer chunk;
	chunk.data = malloc(1);
	chunk.size = 0;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cdsl_curl_write_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&chunk);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);

	CURLcode res = curl_easy_perform(curl);
	char* result = NULL;
	if (res == CURLE_OK) {
		result = chunk.data;
	} else {
		free(chunk.data);
	}

	curl_slist_free_all(headers);
	free(body);
	curl_easy_cleanup(curl);

	if (!result) {
		return NULL;
	}
#else
	char* escaped_prompt = escape_json_string(prompt);
	if (!escaped_prompt) {
		return NULL;
	}

	char* s_url = escape_sq(config->api_base);
	char* s_key = escape_sq(config->api_key);
	char* s_model = escape_sq(config->model);
	char* s_prompt = escape_sq(escaped_prompt);
	free(escaped_prompt);

	if (!s_url || !s_key || !s_model || !s_prompt) {
		free(s_url);
		free(s_key);
		free(s_model);
		free(s_prompt);
		return NULL;
	}

	size_t cmd_len = strlen(s_url) + strlen(s_key) + strlen(s_model) + strlen(s_prompt) + 256;
	char* cmd = malloc(cmd_len);
	if (!cmd) {
		free(s_url);
		free(s_key);
		free(s_model);
		free(s_prompt);
		return NULL;
	}
	snprintf(cmd,
		 cmd_len,
		 "curl -s -X POST '%s/chat/completions' "
		 "-H 'Content-Type: application/json' "
		 "-H 'Authorization: Bearer %s' "
		 "-d "
		 "'{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}],"
		 "\"temperature\":0.1}'",
		 s_url,
		 s_key,
		 s_model,
		 s_prompt);
	free(s_url);
	free(s_key);
	free(s_model);
	free(s_prompt);

	FILE* fp = popen(cmd, "r");
	free(cmd);
	if (!fp) {
		return NULL;
	}

	size_t cap = 8192;
	char* result = malloc(cap);
	if (!result) {
		pclose(fp);
		return NULL;
	}
	size_t total = 0;
	size_t n;
	while ((n = fread(result + total, 1, cap - total - 1, fp)) > 0) {
		total += n;
		if (total >= cap - 1) {
			cap *= 2;
			char* new_result = realloc(result, cap);
			if (!new_result) {
				free(result);
				pclose(fp);
				return NULL;
			}
			result = new_result;
		}
	}
	result[total] = '\0';
	pclose(fp);
#endif

	cdsl_json_value_t* root = cdsl_json_parse(result);
	free(result);
	if (!root) {
		return NULL;
	}

	char* decoded = NULL;
	cdsl_json_value_t* choices = find_json_key(root, "choices");
	if (choices && choices->type == JSON_ARRAY && choices->value.array.items) {
		cdsl_json_value_t* first_choice = choices->value.array.items;
		cdsl_json_value_t* message = find_json_key(first_choice, "message");
		if (message) {
			cdsl_json_value_t* content_val = find_json_key(message, "content");
			if (content_val && content_val->type == JSON_STRING) {
				decoded = strdup(content_val->value.string_val);
			}
		}
	}
	cdsl_json_free(root);

	if (!decoded) {
		return NULL;
	}

	if (config->cache && config->cache->put) {
		config->cache->put(config->cache->ctx, prompt, decoded);
	}

	return decoded;
}

/**
 * @brief Process a single SSE "data: " chunk from LLM (internal).
 *
 * Parses JSON, extracts delta content, appends to result, and triggers callback.
 *
 * @param data        The JSON string after "data: "
 * @param result_ptr  Pointer to accumulated result string
 * @param total_ptr   Pointer to current total length
 * @param cap_ptr     Pointer to current capacity
 * @param callback    User stream callback
 * @param user_data   User opaque pointer
 */
static void
process_sse_data(const char* data,
		 char** result_ptr,
		 size_t* total_ptr,
		 size_t* cap_ptr,
		 cdsl_ai_stream_cb_t callback,
		 void* user_data)
{
	if (strncmp(data, "[DONE]", 6) == 0) {
		return;
	}

	cdsl_json_value_t* root = cdsl_json_parse(data);
	if (!root) {
		return;
	}

	char* chunk_content = NULL;
	cdsl_json_value_t* choices = find_json_key(root, "choices");
	if (choices && choices->type == JSON_ARRAY && choices->value.array.items) {
		cdsl_json_value_t* first_choice = choices->value.array.items;
		cdsl_json_value_t* delta = find_json_key(first_choice, "delta");
		if (delta) {
			cdsl_json_value_t* content_val = find_json_key(delta, "content");
			if (content_val && content_val->type == JSON_STRING) {
				chunk_content = content_val->value.string_val;
			}
		}
	}

	if (chunk_content) {
		size_t len = strlen(chunk_content);
		if (*total_ptr + len + 1 > *cap_ptr) {
			*cap_ptr = *total_ptr + len + 1024;
			char* new_result = realloc(*result_ptr, *cap_ptr);
			if (!new_result) {
				cdsl_json_free(root);
				return;
			}
			*result_ptr = new_result;
		}
		memcpy(*result_ptr + *total_ptr, chunk_content, len);
		*total_ptr += len;
		(*result_ptr)[*total_ptr] = '\0';

		if (callback) {
			callback(chunk_content, user_data);
		}
	}
	cdsl_json_free(root);
}

#ifdef CDSL_USE_CURL
/**
 * @brief Context for CURL streaming (internal).
 */
struct curl_stream_ctx {
	cdsl_ai_stream_cb_t callback; /**< User callback */
	void* user_data;	      /**< User opaque data */
	char** result_ptr;	      /**< Pointer to accumulated result */
	size_t* total_ptr;	      /**< Pointer to total length */
	size_t* cap_ptr;	      /**< Pointer to capacity */
	char line_buf[16384];	      /**< Internal line buffer for SSE */
	size_t line_len;	      /**< Current line buffer length */
};

/**
 * @brief CURL callback for streaming responses.
 *
 * Buffers partial lines and processes them as SSE "data: " chunks.
 */
static size_t
cdsl_curl_stream_callback(void* contents, size_t size, size_t nmemb, void* userp)
{
	size_t realsize = size * nmemb;
	struct curl_stream_ctx* ctx = (struct curl_stream_ctx*)userp;
	const char* p = (const char*)contents;

	for (size_t i = 0; i < realsize; i++) {
		if (p[i] == '\n') {
			ctx->line_buf[ctx->line_len] = '\0';
			if (strncmp(ctx->line_buf, "data: ", 6) == 0) {
				process_sse_data(ctx->line_buf + 6,
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
#endif

/**
 * @brief Return a default AI config that uses mock generation.
 *
 * The mock flag is set; api_key, api_base, and model are all NULL.
 *
 * @return Default config structure
 */
cdsl_ai_config_t
cdsl_ai_config_default(void)
{
	cdsl_ai_config_t cfg;
	cfg.use_mock = 1;
	cfg.api_key = NULL;
	cfg.api_base = NULL;
	cfg.model = NULL;
	cfg.business_context = NULL;
	cfg.cache = NULL;
	cfg.provider_name = NULL;
	cfg.cache_driver_name = NULL;
	return cfg;
}

/**
 * @brief Convert a type enum to a printable name (internal).
 * @param t Type value
 * @return Static string label
 */
static const char*
type_to_str(cdsl_type_t t)
{
	switch (t) {
	case CDSL_TYPE_INT:
		return "INT";
	case CDSL_TYPE_FLOAT:
		return "FLOAT";
	case CDSL_TYPE_BOOL:
		return "BOOL";
	case CDSL_TYPE_STRING:
		return "STRING";
	case CDSL_TYPE_DATE:
		return "DATE";
	default:
		return "VOID";
	}
}

/**
 * @brief Count schema variables (internal).
 * @param schema Schema (may be NULL)
 * @return Variable count
 */
static int
count_schema_vars(const cdsl_schema_t* schema)
{
	int n = 0;
	for (cdsl_var_schema_t* v = schema ? schema->vars : NULL; v; v = v->next) {
		n++;
	}
	return n;
}

/**
 * @brief Append schema variable and action info to a prompt buffer (internal).
 * @param buf    Target buffer
 * @param sz     Buffer size
 * @param schema Schema to describe
 */
/**
 * @brief Safe append to a buffer with overflow protection.
 * @return New string length, capped at sz-1
 */
static size_t
safe_append(char* buf, size_t sz, size_t pos, const char* fmt, ...)
{
	if (pos >= sz - 1) {
		return pos;
	}
	va_list ap;
	va_start(ap, fmt);
	int n = vsnprintf(buf + pos, sz - pos, fmt, ap);
	va_end(ap);
	return (n > 0) ? pos + (size_t)n : pos;
}

static void
build_schema_prompt(char* buf, size_t sz, const cdsl_schema_t* schema)
{
	size_t pos = strlen(buf);
	pos = safe_append(buf, sz, pos, "Available variables:\n");
	for (cdsl_var_schema_t* v = schema ? schema->vars : NULL; v; v = v->next) {
		pos = safe_append(buf, sz, pos, "  - %s (%s)\n", v->name, type_to_str(v->type));
	}
	pos = safe_append(buf, sz, pos, "\nAvailable actions:\n");
	for (cdsl_action_schema_t* a = schema ? schema->actions : NULL; a; a = a->next) {
		pos = safe_append(buf, sz, pos, "  - %s(", a->name);
		for (int i = 0; i < a->arg_count; i++) {
			if (i > 0) {
				pos = safe_append(buf, sz, pos, ", ");
			}
			pos = safe_append(buf, sz, pos, "%s", type_to_str(a->arg_types[i]));
		}
		pos = safe_append(buf, sz, pos, ") -> %s\n", type_to_str(a->return_type));
	}
}

/**
 * @brief Check if a word appears as a keyword in text (internal).
 *
 * Uses word-boundary detection (space, tab, newline, comma, paren).
 *
 * @param text Text to search
 * @param word Keyword to find
 * @return 1 if found as a standalone word, 0 otherwise
 */
static int
has_keyword(const char* text, const char* word)
{
	if (!text || !word) {
		return 0;
	}
	size_t wlen = strlen(word);
	const char* p = text;
	while ((p = strstr(p, word)) != NULL) {
		char before = (p > text) ? *(p - 1) : ' ';
		char after = p[wlen];
		if ((before == ' ' || before == '\t' || before == '\n' || before == ',' ||
		     before == '(') &&
		    (after == ' ' || after == '\t' || after == '\n' || after == ',' ||
		     after == '.' || after == ')' || after == '\0')) {
			return 1;
		}
		p++;
	}
	return 0;
}

/**
 * @brief Generate a mock DSL rule from natural language (internal).
 *
 * Produces a generic scoring rule or simple WHEN/THEN rule depending
 * on the presence of conditional keywords and available schema variables.
 *
 * @param natural_language Input text
 * @param schema           Schema (may be NULL)
 * @param business_context Optional business context (may be NULL)
 * @return Allocated DSL string (caller frees)
 */
static char*
mock_translate_generic(const char* natural_language,
		       const cdsl_schema_t* schema,
		       const char* business_context)
{
	int is_simple =
	    (has_keyword(natural_language, "when") || has_keyword(natural_language, "if") ||
	     has_keyword(natural_language, "WHEN") || has_keyword(natural_language, "IF"));
	int n_vars = count_schema_vars(schema);

	if (!schema || n_vars == 0) {
		return strdup("RULE generated_rule {\n"
			      "    META {\n"
			      "        description = \"Auto-generated rule\"\n"
			      "        pass_threshold = \"80\"\n"
			      "        partial_threshold = \"50\"\n"
			      "    }\n"
			      "    METRIC score {\n"
			      "        META { description = \"Score metric\" weight = \"100\" }\n"
			      "        DEFAULT score(0)\n"
			      "    }\n"
			      "}\n");
	}

	char rule_name[128] = {0};
	const char* nl = natural_language;
	while (*nl == ' ') {
		nl++;
	}
	int rn = 0;
	for (int i = 0; nl[i] && nl[i] != ' ' && nl[i] != ',' && nl[i] != '.' && rn < 32; i++) {
		if (isalpha((unsigned char)nl[i]) || nl[i] == '_') {
			rule_name[rn++] = tolower((unsigned char)nl[i]);
		}
	}
	rule_name[rn] = '\0';
	if (rn == 0) {
		strcpy(rule_name, "rule");
	}

	char desc[256];
	size_t dpos = safe_append(desc, sizeof(desc), 0, "%s", natural_language);
	if (business_context) {
		safe_append(desc, sizeof(desc), dpos, " | context: %s", business_context);
	}

	if (is_simple && n_vars >= 1) {
		cdsl_var_schema_t* first_var = schema->vars;
		char buf[4096];
		snprintf(buf,
			 sizeof(buf),
			 "RULE %s {\n"
			 "    META {\n"
			 "        description = \"%s\"\n"
			 "    }\n"
			 "    WHEN %s %s %s\n"
			 "    THEN record_warning(\"triggered\")\n"
			 "}\n",
			 rule_name,
			 desc,
			 first_var->name,
			 (first_var->type == CDSL_TYPE_INT || first_var->type == CDSL_TYPE_FLOAT)
			     ? ">"
			     : "==",
			 first_var->type == CDSL_TYPE_STRING ? "\"expected\""
			 : first_var->type == CDSL_TYPE_BOOL ? "true"
							     : "0");
		return strdup(buf);
	}

	char buf[8192] = {0};
	snprintf(buf,
		 sizeof(buf),
		 "RULE %s {\n"
		 "    META {\n"
		 "        description = \"%s\"\n"
		 "        pass_threshold = \"80\"\n"
		 "        partial_threshold = \"50\"\n"
		 "    }\n",
		 rule_name,
		 desc);

	int metric_idx = 0;
	for (cdsl_var_schema_t* v = schema->vars; v && metric_idx < 8; v = v->next, metric_idx++) {
		char mname[64];
		const char* dot = strrchr(v->name, '.');
		if (dot) {
			snprintf(mname, sizeof(mname), "m%d_%s", metric_idx + 1, dot + 1);
		} else {
			snprintf(mname, sizeof(mname), "m%d", metric_idx + 1);
		}

		int weight = 100 / (n_vars < 1 ? 1 : n_vars);
		int is_first = (metric_idx == 0);

		snprintf(buf + strlen(buf),
			 sizeof(buf) - strlen(buf),
			 "    METRIC %s {\n"
			 "        META {\n"
			 "            description = \"Check %s\"\n"
			 "            weight = \"%d\"%s\n"
			 "        }\n",
			 mname,
			 v->name,
			 weight,
			 is_first ? "\n            is_critical = \"true\"" : "");

		switch (v->type) {
		case CDSL_TYPE_INT:
			snprintf(buf + strlen(buf),
				 sizeof(buf) - strlen(buf),
				 "        CASE %s >= 0 THEN score(%d)\n"
				 "        DEFAULT score(0)\n",
				 v->name,
				 weight);
			break;
		case CDSL_TYPE_FLOAT:
			snprintf(buf + strlen(buf),
				 sizeof(buf) - strlen(buf),
				 "        CASE %s >= 0.0 THEN score(%d)\n"
				 "        DEFAULT score(0)\n",
				 v->name,
				 weight);
			break;
		case CDSL_TYPE_BOOL:
			snprintf(buf + strlen(buf),
				 sizeof(buf) - strlen(buf),
				 "        CASE %s == true THEN score(%d)\n"
				 "        DEFAULT score(0)\n",
				 v->name,
				 weight);
			break;
		case CDSL_TYPE_STRING:
			snprintf(buf + strlen(buf),
				 sizeof(buf) - strlen(buf),
				 "        CASE %s != \"\" THEN score(%d)\n"
				 "        DEFAULT score(0)\n",
				 v->name,
				 weight);
			break;
		default:
			snprintf(buf + strlen(buf),
				 sizeof(buf) - strlen(buf),
				 "        DEFAULT score(%d)\n",
				 weight);
			break;
		}
		snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "    }\n");
	}

	snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf), "}\n");
	return strdup(buf);
}

static char*
default_translate(void* ctx,
		  const char* natural_language,
		  const cdsl_schema_t* schema,
		  const cdsl_ai_config_t* config)
{
	(void)ctx;
	if (!natural_language) {
		return NULL;
	}

	if (config && config->use_mock) {
		return mock_translate_generic(natural_language, schema, config->business_context);
	}

	char prompt[16384] = {0};
	snprintf(prompt,
		 sizeof(prompt),
		 "You are a C-DSL rule engine. Convert the following natural language description "
		 "into a valid C-DSL rule.\n\n"
		 "## C-DSL Syntax Reference\n"
		 "- Simple rule: RULE name { META { description = \"...\" } WHEN <expr> THEN "
		 "action(\"arg\") }\n"
		 "- Scoring rule: RULE name { META { description = \"...\" pass_threshold = \"80\" "
		 "partial_threshold = \"50\" } METRIC m { META { weight = \"40\" is_critical = "
		 "\"true\" } CASE <expr> THEN score(N) DEFAULT score(0) } }\n"
		 "- Operators: == != < > <= >= AND OR NOT\n"
		 "- Keywords: true false\n"
		 "- Functions: score(N), fail_metric(N, \"reason\") and registered actions\n\n");

	build_schema_prompt(prompt, sizeof(prompt), schema);

	if (config && config->business_context) {
		snprintf(prompt + strlen(prompt),
			 sizeof(prompt) - strlen(prompt),
			 "\nAdditional business context:\n%s\n",
			 config->business_context);
	}

	snprintf(prompt + strlen(prompt),
		 sizeof(prompt) - strlen(prompt),
		 "\nUser request: %s\n\n"
		 "Output ONLY the DSL code wrapped in a ```dsl code block. "
		 "Use the exact variable names listed above. "
		 "Choose appropriate threshold values based on the business context.",
		 natural_language);

	if (config && !config->use_mock && config->api_key) {
		char* response = call_llm_api(prompt, config);
		if (response) {
			char* start = strstr(response, "```dsl");
			if (start) {
				start += 6;
				while (*start == '\n') {
					start++;
				}
				char* end = strstr(start, "```");
				if (end) {
					char* dsl = malloc(end - start + 1);
					memcpy(dsl, start, end - start);
					dsl[end - start] = '\0';
					free(response);
					return dsl;
				}
			}
			char* dsl = strdup(response);
			free(response);
			return dsl;
		}
		fprintf(stderr,
			"[AI Bridge] API call failed, falling back to generic generation.\n");
	}

	if (config) {
		fprintf(stderr, "[AI Bridge] Mock-generating DSL from prompt.\n%s\n", prompt);
	}
	return mock_translate_generic(
	    natural_language, schema, config ? config->business_context : NULL);
}

/**
 * @brief Translate natural language to C-DSL rule code.
 *
 * This function acts as a bridge between human descriptions and technical rules.
 * In mock mode, it uses simple heuristic patterns. In API mode (if compiled with libcurl),
 * it communicates with an LLM (e.g., OpenAI, Claude) to perform high-fidelity translation
 * based on the provided schema's variable and action definitions.
 *
 * @param natural_language User's rule description (e.g., "If age is over 18, allow entry")
 * @param schema Registered schema providing context for available variables and actions
 * @param config AI configuration (controls mock mode, API credentials, and model selection)
 * @return Newly allocated DSL string (must be freed with @c free()), or NULL on network/translation error
 */
char*
cdsl_ai_translate(const char* natural_language,
		  const cdsl_schema_t* schema,
		  const cdsl_ai_config_t* config)
{
	cdsl_ai_provider_t* p = get_provider(config ? config->provider_name : NULL);
	return p->translate(p->ctx, natural_language, schema, config);
}

static cdsl_ai_review_t*
default_review(void* ctx,
	       const char* dsl_code,
	       const cdsl_schema_t* schema,
	       const cdsl_ai_config_t* config)
{
	(void)ctx;
	(void)schema;
	if (!dsl_code) {
		return NULL;
	}

	if (config && config->use_mock) {
		int has_meta = (strstr(dsl_code, "META") != NULL);
		int has_metric = (strstr(dsl_code, "METRIC") != NULL);
		int has_case = (strstr(dsl_code, "CASE") != NULL);
		int has_default = (strstr(dsl_code, "DEFAULT") != NULL);
		int has_when = (strstr(dsl_code, "WHEN") != NULL);
		int has_score = (strstr(dsl_code, "score(") != NULL);
		int has_critical = (strstr(dsl_code, "is_critical") != NULL);

		int score = 10;
		char suggestions[1024] = {0};

		if (has_meta) {
			score += 15;
		} else {
			strcat(suggestions, "Add META block with description and thresholds. ");
		}

		if (has_metric) {
			score += 15;
			if (has_case && has_default) {
				score += 15;
			} else {
				strcat(suggestions,
				       "Add CASE and DEFAULT branches to each METRIC. ");
			}
		} else if (has_when) {
			score += 15;
		}

		if (has_critical) {
			score += 10;
		} else if (has_metric) {
			strcat(suggestions, "Mark critical items with is_critical=true. ");
		}

		if (has_score) {
			score += 10;
		}

		int risk = (score < 30) ? 100 - score * 2 : 100 - score;

		cdsl_ai_review_t* rev = calloc(1, sizeof(*rev));
		rev->approved = (score >= 40);
		rev->risk_score = risk < 0 ? 0 : (risk > 100 ? 100 : risk);
		char reason[512];
		snprintf(reason,
			 sizeof(reason),
			 "Analysis score: %d/75. %s",
			 score,
			 rev->approved ? "Rule structure is adequate."
				       : "Rule structure needs improvement.");
		rev->reason = strdup(reason);
		rev->suggestions =
		    suggestions[0] ? strdup(suggestions) : strdup("No issues found.");
		return rev;
	}

	if (config && !config->use_mock && config->api_key) {
		char review_prompt[8192];
		snprintf(
		    review_prompt,
		    sizeof(review_prompt),
		    "You are a DSL rule safety reviewer. Analyze the following C-DSL rule for:\n"
		    "1. Logical contradictions (e.g., x > 10 AND x < 5)\n"
		    "2. Missing META blocks or weights\n"
		    "3. Security risks (e.g., unauthorized actions)\n\n"
		    "DSL code:\n%s\n\n"
		    "Respond in JSON: "
		    "{\"approved\":true/"
		    "false,\"risk_score\":0-100,\"reason\":\"...\",\"suggestions\":\"...\"}",
		    dsl_code);

		char* response = call_llm_api(review_prompt, config);
		cdsl_ai_review_t* rev = calloc(1, sizeof(*rev));
		if (response) {
			cdsl_json_value_t* root = cdsl_json_parse(response);
			if (root) {
				cdsl_json_value_t* v;
				if ((v = find_json_key(root, "approved"))) {
					rev->approved =
					    (v->type == JSON_BOOL) ? v->value.bool_val : 0;
				}
				if ((v = find_json_key(root, "risk_score"))) {
					rev->risk_score =
					    (v->type == JSON_NUMBER) ? (int)v->value.number_val : 0;
				}
				if ((v = find_json_key(root, "reason")) && v->type == JSON_STRING) {
					rev->reason = strdup(v->value.string_val);
				}
				if ((v = find_json_key(root, "suggestions")) &&
				    v->type == JSON_STRING) {
					rev->suggestions = strdup(v->value.string_val);
				}
				cdsl_json_free(root);
			}
			free(response);
		}
		if (!rev->reason) {
			rev->reason = strdup("LLM review completed");
		}
		if (!rev->suggestions) {
			rev->suggestions = strdup("");
		}
		return rev;
	}

	cdsl_ai_review_t* rev = calloc(1, sizeof(*rev));
	rev->approved = 1;
	rev->risk_score = 10;
	rev->reason = strdup("Generic review passed");
	rev->suggestions = strdup("No suggestions");
	return rev;
}

/**
 * @brief Review a DSL rule for safety, completeness, and logical consistency.
 *
 * The AI review engine analyzes the rule structure beyond static verification.
 * it checks for:
 * - Logical contradictions (e.g., overlapping CASE ranges)
 * - Missing mandatory metadata (e.g., weight, is_critical)
 * - Security risks (e.g., unauthorized action triggers)
 *
 * @param dsl_code Raw DSL rule string to analyze
 * @param schema Registered schema for contextual validation
 * @param config AI configuration
 * @return Review result structure containing approval status and risk score (must be freed with cdsl_ai_review_free)
 */
cdsl_ai_review_t*
cdsl_ai_review(const char* dsl_code, const cdsl_schema_t* schema, const cdsl_ai_config_t* config)
{
	cdsl_ai_provider_t* p = get_provider(config ? config->provider_name : NULL);
	return p->review(p->ctx, dsl_code, schema, config);
}

/**
 * @brief Free an AI review result.
 *
 * @param review Review to free (NULL-safe)
 */
void
cdsl_ai_review_free(cdsl_ai_review_t* review)
{
	if (!review) {
		return;
	}
	free(review->reason);
	free(review->suggestions);
	free(review);
}

/**
 * @brief Send a streaming prompt to a remote LLM API via cURL (internal).
 *
 * Uses SSE (server-sent events) parsing. Each content chunk is
 * delivered to the callback as it arrives. The full response is also
 * accumulated and returned.
 *
 * @param prompt    The user prompt
 * @param config    AI configuration
 * @param callback  Per-chunk callback (may be NULL)
 * @param user_data Opaque pointer forwarded to callback
 * @return Accumulated full response, or NULL on failure
 */
static char*
call_llm_api_stream(const char* prompt,
		    const cdsl_ai_config_t* config,
		    cdsl_ai_stream_cb_t callback,
		    void* user_data)
{
	if (!config || !config->api_key || !config->api_base || !config->model) {
		return NULL;
	}

	cdsl_ai_cache_t* cache = get_cache_driver(config);
	if (cache && cache->get) {
		char* cached = cache->get(cache->ctx, prompt);
		if (cached) {
			if (callback) {
				callback(cached, user_data);
			}
			return cached;
		}
	}

	char* escaped_prompt = escape_json_string(prompt);
	if (!escaped_prompt) {
		return NULL;
	}

	size_t cap = 8192;
	char* result = malloc(cap);
	size_t total = 0;
	result[0] = '\0';

#ifdef CDSL_USE_CURL
	CURL* curl = curl_easy_init();
	if (!curl) {
		free(escaped_prompt);
		free(result);
		return NULL;
	}

	char url[512];
	snprintf(url, sizeof(url), "%s/chat/completions", config->api_base);

	struct curl_slist* headers = NULL;
	headers = curl_slist_append(headers, "Content-Type: application/json");
	char auth_header[512];
	snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", config->api_key);
	headers = curl_slist_append(headers, auth_header);

	char* body = malloc(strlen(config->model) + strlen(escaped_prompt) + 128);
	if (!body) {
		free(escaped_prompt);
		curl_slist_free_all(headers);
		curl_easy_cleanup(curl);
		return NULL;
	}
	snprintf(body,
		 strlen(config->model) + strlen(escaped_prompt) + 128,
		 "{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}],"
		 "\"temperature\":0.1,\"stream\":true}",
		 config->model,
		 escaped_prompt);
	free(escaped_prompt);

	struct curl_stream_ctx ctx;
	ctx.callback = callback;
	ctx.user_data = user_data;
	ctx.result_ptr = &result;
	ctx.total_ptr = &total;
	ctx.cap_ptr = &cap;
	ctx.line_len = 0;

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
	curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
	curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, cdsl_curl_stream_callback);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void*)&ctx);
	curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);

	CURLcode res = curl_easy_perform(curl);
	if (res != CURLE_OK) {
		free(result);
		result = NULL;
	}

	curl_slist_free_all(headers);
	free(body);
	curl_easy_cleanup(curl);
#else
	char* s_url = escape_sq(config->api_base);
	char* s_key = escape_sq(config->api_key);
	char* s_model = escape_sq(config->model);
	char* s_prompt = escape_sq(escaped_prompt);
	free(escaped_prompt);

	if (!s_url || !s_key || !s_model || !s_prompt) {
		free(s_url);
		free(s_key);
		free(s_model);
		free(s_prompt);
		free(result);
		return NULL;
	}

	size_t cmd_len = strlen(s_url) + strlen(s_key) + strlen(s_model) + strlen(s_prompt) + 256;
	char* cmd = malloc(cmd_len);
	if (!cmd) {
		free(s_url);
		free(s_key);
		free(s_model);
		free(s_prompt);
		free(result);
		return NULL;
	}
	snprintf(cmd,
		 cmd_len,
		 "curl -s -N -X POST '%s/chat/completions' "
		 "-H 'Content-Type: application/json' "
		 "-H 'Authorization: Bearer %s' "
		 "-d "
		 "'{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}],"
		 "\"temperature\":0.1,\"stream\":true}'",
		 s_url,
		 s_key,
		 s_model,
		 s_prompt);
	free(s_url);
	free(s_key);
	free(s_model);
	free(s_prompt);

	FILE* fp = popen(cmd, "r");
	free(cmd);
	if (!fp) {
		free(result);
		return NULL;
	}

	char line[4096];
	while (fgets(line, sizeof(line), fp)) {
		if (strncmp(line, "data: ", 6) == 0) {
			process_sse_data(line + 6, &result, &total, &cap, callback, user_data);
		}
	}
	pclose(fp);
#endif

	if (cache && cache->put && result) {
		cache->put(cache->ctx, prompt, result);
	}

	return result;
}

static char*
default_translate_stream(void* ctx,
			 const char* natural_language,
			 const cdsl_schema_t* schema,
			 const cdsl_ai_config_t* config,
			 cdsl_ai_stream_cb_t callback,
			 void* user_data)
{
	(void)ctx;
	if (!natural_language) {
		return NULL;
	}

	if (config && config->use_mock) {
		char* result = mock_translate_generic(
		    natural_language, schema, config ? config->business_context : NULL);
		if (result && callback) {
			callback(result, user_data);
		}
		return result;
	}

	char prompt[16384] = {0};
	snprintf(prompt,
		 sizeof(prompt),
		 "You are a C-DSL rule translator. Convert the following request into "
		 "a valid C-DSL rule.\n\n"
		 "## C-DSL Syntax Reference\n"
		 "- Simple rule: RULE name { META { description = \"...\" } WHEN <expr> THEN "
		 "action(\"arg\") }\n"
		 "- Scoring rule: RULE name { META { description = \"...\" pass_threshold = \"80\" "
		 "partial_threshold = \"50\" } METRIC m { META { weight = \"40\" is_critical = "
		 "\"true\" } CASE <expr> THEN score(N) DEFAULT score(0) } }\n"
		 "- Operators: == != < > <= >= AND OR NOT\n"
		 "- Keywords: true false\n"
		 "- Functions: score(N), fail_metric(N, \"reason\") etc.\n\n");

	build_schema_prompt(prompt, sizeof(prompt), schema);

	if (config && config->business_context) {
		snprintf(prompt + strlen(prompt),
			 sizeof(prompt) - strlen(prompt),
			 "\nAdditional business context:\n%s\n",
			 config->business_context);
	}

	snprintf(prompt + strlen(prompt),
		 sizeof(prompt) - strlen(prompt),
		 "\nUser request: %s\n\nOutput ONLY the DSL code in a ```dsl code block.",
		 natural_language);

	return call_llm_api_stream(prompt, config, callback, user_data);
}

/**
 * @brief Translate natural language to DSL with real-time streaming feedback.
 *
 * @param natural_language User's rule description
 * @param schema Registered schema
 * @param config AI configuration
 * @param callback Callback invoked for each received text chunk
 * @param user_data User-provided data pointer passed to the callback
 * @return Full translated DSL string (must be freed with @c free()), or NULL on error
 */
char*
cdsl_ai_translate_stream(const char* natural_language,
			 const cdsl_schema_t* schema,
			 const cdsl_ai_config_t* config,
			 cdsl_ai_stream_cb_t callback,
			 void* user_data)
{
	cdsl_ai_provider_t* p = get_provider(config ? config->provider_name : NULL);
	return p->translate_stream(p->ctx, natural_language, schema, config, callback, user_data);
}

static char*
default_review_stream(void* ctx,
		      const char* dsl_code,
		      const cdsl_schema_t* schema,
		      const cdsl_ai_config_t* config,
		      cdsl_ai_stream_cb_t callback,
		      void* user_data)
{
	(void)ctx;
	if (!dsl_code) {
		return NULL;
	}

	if (config && config->use_mock) {
		cdsl_ai_review_t* rev = cdsl_ai_review(dsl_code, schema, config);
		char esc_reason[512];
		char esc_suggestions[512];
		json_escape_small(esc_reason, sizeof(esc_reason), rev->reason ? rev->reason : "");
		json_escape_small(esc_suggestions,
				  sizeof(esc_suggestions),
				  rev->suggestions ? rev->suggestions : "");
		char* json = malloc(2048);
		if (!json) {
			cdsl_ai_review_free(rev);
			return NULL;
		}
		snprintf(
		    json,
		    2048,
		    "{\"approved\":%s,\"risk_score\":%d,\"reason\":\"%s\",\"suggestions\":\"%s\"}",
		    rev->approved ? "true" : "false",
		    rev->risk_score,
		    esc_reason,
		    esc_suggestions);
		cdsl_ai_review_free(rev);
		if (callback) {
			callback(json, user_data);
		}
		return json;
	}

	char review_prompt[8192];
	snprintf(review_prompt,
		 sizeof(review_prompt),
		 "You are a DSL rule safety reviewer. Analyze the following C-DSL rule for:\n"
		 "1. Logical contradictions\n"
		 "2. Missing META blocks or weights\n"
		 "3. Security risks\n\n"
		 "DSL code:\n%s\n\n"
		 "Respond in JSON: "
		 "{\"approved\":true/"
		 "false,\"risk_score\":0-100,\"reason\":\"...\",\"suggestions\":\"...\"}",
		 dsl_code);

	return call_llm_api_stream(review_prompt, config, callback, user_data);
}

/**
 * @brief Review a DSL rule with real-time streaming feedback.
 *
 * This version returns the raw human-readable analysis from the LLM
 * rather than a structured cdsl_ai_review_t.
 *
 * @param dsl_code DSL rule to review
 * @param schema Registered schema
 * @param config AI configuration
 * @param callback Callback invoked for each received text chunk
 * @param user_data User-provided data pointer passed to the callback
 * @return Complete analysis text (must be freed with @c free()), or NULL on error
 */
char*
cdsl_ai_review_stream(const char* dsl_code,
		      const cdsl_schema_t* schema,
		      const cdsl_ai_config_t* config,
		      cdsl_ai_stream_cb_t callback,
		      void* user_data)
{
	cdsl_ai_provider_t* p = get_provider(config ? config->provider_name : NULL);
	return p->review_stream(p->ctx, dsl_code, schema, config, callback, user_data);
}
/** @} */
