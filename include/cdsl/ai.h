/**
 * @file cdsl/ai.h
 * @brief AI integration layer: natural language to DSL translation and rule review.
 *
 * Provides functions to translate natural language rule descriptions into
 * C-DSL syntax, and to review DSL rules for structural integrity and
 * safety. Supports both offline mock mode and real LLM API calls.
 *
 * @defgroup ai_bridge AI Bridge Layer
 * @{
 */

#ifndef CDSL_AI_H
#define CDSL_AI_H

#include "cdsl/schema.h"

/**
 * @brief External cache interface for AI responses.
 *
 * Allows users to inject custom caching logic (e.g., in-memory, Redis, file-based)
 * to avoid redundant LLM API calls for identical prompts.
 */
typedef struct {
	void* ctx; /**< Opaque pointer for the cache implementation's state */

	/**
	 * @brief Retrieve a cached response.
	 * @param ctx Opaque state pointer.
	 * @param key The prompt string used as the cache key.
	 * @return Allocated string containing the cached response, or NULL if miss.
	 *         The caller is responsible for freeing the returned string.
	 */
	char* (*get)(void* ctx, const char* key);

	/**
	 * @brief Store a response in the cache.
	 * @param ctx Opaque state pointer.
	 * @param key The prompt string used as the cache key.
	 * @param value The response string to cache.
	 */
	void (*put)(void* ctx, const char* key, const char* value);
} cdsl_ai_cache_t;

typedef struct cdsl_ai_config {
	int use_mock;		 /**< 1 = use offline mock translation, 0 = use LLM API */
	char* api_key;		 /**< API key for LLM service (e.g. OpenAI) */
	char* api_base;		 /**< API base URL (e.g. "https://api.openai.com/v1") */
	char* model;		 /**< Model name (e.g. "gpt-4o-mini") */
	char* business_context;	 /**< Optional business context to guide DSL generation */
	cdsl_ai_cache_t* cache;	 /**< Optional external cache implementation */
	char* provider_name;	 /**< Provider name (e.g. "default", "langchain") */
	char* cache_driver_name; /**< Global cache driver name (e.g. "redis") */
} cdsl_ai_config_t;

/**
 * @brief AI rule safety review result.
 *
 * Returned by cdsl_ai_review() after analyzing a DSL rule for
 * logical contradictions, missing elements, and security risks.
 */
typedef struct cdsl_ai_review {
	int approved;	   /**< 1 if rule passed review, 0 if rejected */
	int risk_score;	   /**< Risk score 0-100 (0 = no risk) */
	char* reason;	   /**< Human-readable explanation */
	char* suggestions; /**< Improvement suggestions */
} cdsl_ai_review_t;

/**
 * @brief External AI provider interface.
 */
typedef struct {
	void* ctx; /**< Opaque pointer for the provider's state */

	/**
	 * @brief Translate natural language to DSL.
	 */
	char* (*translate)(void* ctx,
			   const char* nl,
			   const cdsl_schema_t* schema,
			   const cdsl_ai_config_t* cfg);

	/**
	 * @brief Review a DSL rule for safety.
	 */
	cdsl_ai_review_t* (*review)(void* ctx,
				    const char* dsl,
				    const cdsl_schema_t* schema,
				    const cdsl_ai_config_t* cfg);

	/**
	 * @brief Streaming translation.
	 */
	char* (*translate_stream)(void* ctx,
				  const char* nl,
				  const cdsl_schema_t* schema,
				  const cdsl_ai_config_t* cfg,
				  void (*callback)(const char*, void*),
				  void* user_data);

	/**
	 * @brief Streaming review.
	 */
	char* (*review_stream)(void* ctx,
			       const char* dsl,
			       const cdsl_schema_t* schema,
			       const cdsl_ai_config_t* cfg,
			       void (*callback)(const char*, void*),
			       void* user_data);
} cdsl_ai_provider_t;

/**
 * @brief Register a custom AI provider.
 * @param name Unique name for the provider
 * @param provider Interface implementation
 */
void cdsl_ai_register_provider(const char* name, const cdsl_ai_provider_t* provider);

/**
 * @brief Register a custom cache driver (singleton-style access).
 * @param name Unique name for the driver
 * @param cache Interface implementation
 */
void cdsl_ai_register_cache_driver(const char* name, const cdsl_ai_cache_t* cache);

/**
 * @brief Get default AI config (mock mode enabled).
 * @return Default configuration with use_mock=1
 */
cdsl_ai_config_t cdsl_ai_config_default(void);

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
char* cdsl_ai_translate(const char* natural_language,
			const cdsl_schema_t* schema,
			const cdsl_ai_config_t* config);

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
cdsl_ai_review(const char* dsl_code, const cdsl_schema_t* schema, const cdsl_ai_config_t* config);

/**
 * @brief Free an AI review result and all its internal strings.
 *
 * @param review Review result to free (NULL-safe)
 */
void cdsl_ai_review_free(cdsl_ai_review_t* review);

/**
 * @brief Streaming callback for AI responses.
 *
 * Used during long-running translations or reviews to provide real-time
 * feedback to the user interface.
 *
 * @param chunk Current text chunk received from the LLM
 * @param user_data Opaque pointer passed from the original call
 */
typedef void (*cdsl_ai_stream_cb_t)(const char* chunk, void* user_data);

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
char* cdsl_ai_translate_stream(const char* natural_language,
			       const cdsl_schema_t* schema,
			       const cdsl_ai_config_t* config,
			       cdsl_ai_stream_cb_t callback,
			       void* user_data);

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
char* cdsl_ai_review_stream(const char* dsl_code,
			    const cdsl_schema_t* schema,
			    const cdsl_ai_config_t* config,
			    cdsl_ai_stream_cb_t callback,
			    void* user_data);

#endif
/** @} */
