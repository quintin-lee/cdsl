/**
 * @file ai_bridge.h
 * @brief AI integration layer: natural language to DSL translation and rule review.
 *
 * Provides functions to translate natural language rule descriptions into
 * C-DSL syntax, and to review DSL rules for structural integrity and
 * safety. Supports both offline mock mode and real LLM API calls.
 *
 * @defgroup ai_bridge AI Bridge Layer
 * @{
 */

#ifndef CDSL_AI_BRIDGE_H
#define CDSL_AI_BRIDGE_H

#include "abstract.h"

/**
 * @brief AI rule safety review result.
 *
 * Returned by cdsl_ai_review() after analyzing a DSL rule for
 * logical contradictions, missing elements, and security risks.
 */
typedef struct {
	int approved;	   /**< 1 if rule passed review, 0 if rejected */
	int risk_score;	   /**< Risk score 0-100 (0 = no risk) */
	char* reason;	   /**< Human-readable explanation */
	char* suggestions; /**< Improvement suggestions */
} cdsl_ai_review_t;

/**
 * @brief AI bridge configuration.
 *
 * Controls whether to use mock mode (offline) or a real LLM API.
 * For API mode, provide api_key, api_base, and model.
 *
 * @code
 * cdsl_ai_config_t cfg = {
 *     .use_mock = 0,
 *     .api_key = getenv("OPENAI_API_KEY"),
 *     .api_base = "https://api.openai.com/v1",
 *     .model = "gpt-4o-mini"
 * };
 * @endcode
 */
typedef struct {
	int use_mock;		/**< 1 = use offline mock translation, 0 = use LLM API */
	char* api_key;		/**< API key for LLM service (e.g. OpenAI) */
	char* api_base;		/**< API base URL (e.g. "https://api.openai.com/v1") */
	char* model;		/**< Model name (e.g. "gpt-4o-mini") */
	char* business_context; /**< Optional business context to guide DSL generation */
} cdsl_ai_config_t;

/**
 * @brief Get default AI config (mock mode enabled).
 * @return Default configuration with use_mock=1
 */
cdsl_ai_config_t cdsl_ai_config_default(void);

/**
 * @brief Translate natural language to C-DSL rule code.
 *
 * In mock mode, uses keyword matching to generate appropriate DSL.
 * In API mode, sends the prompt to the configured LLM endpoint.
 *
 * @param natural_language User's rule description in natural language
 * @param schema Registered schema (provides available variables/actions)
 * @param config AI configuration (mock or API settings)
 * @return Dynamically allocated DSL string (must be freed with free())
 */
char* cdsl_ai_translate(const char* natural_language,
			const cdsl_schema_t* schema,
			const cdsl_ai_config_t* config);

/**
 * @brief Review a DSL rule for safety and correctness.
 *
 * Analyzes the rule for:
 * - Missing META blocks or weights
 * - Structural completeness (CASE/DEFAULT coverage)
 * - Critical compliance items
 *
 * @param dsl_code DSL rule string to review
 * @param schema Registered schema for context
 * @param config AI configuration
 * @return Review result (must be freed with cdsl_ai_review_free)
 */
cdsl_ai_review_t*
cdsl_ai_review(const char* dsl_code, const cdsl_schema_t* schema, const cdsl_ai_config_t* config);

/**
 * @brief Free an AI review result.
 * @param review Review result to free (NULL-safe)
 */
void cdsl_ai_review_free(cdsl_ai_review_t* review);

/**
 * @brief Streaming callback for AI responses.
 *
 * Called for each chunk of text received from the streaming API.
 *
 * @param chunk Text chunk received
 * @param user_data User-provided data pointer
 */
typedef void (*cdsl_ai_stream_cb_t)(const char* chunk, void* user_data);

/**
 * @brief Stream translate natural language to DSL.
 *
 * Sends the prompt to the LLM API with streaming enabled.
 * Each chunk of the response is delivered via the callback.
 *
 * @param natural_language User's rule description
 * @param schema Registered schema
 * @param config AI configuration
 * @param callback Streaming callback function
 * @param user_data User data passed to callback
 * @return Complete response string (must be freed with free()), or NULL on error
 */
char* cdsl_ai_translate_stream(const char* natural_language,
			       const cdsl_schema_t* schema,
			       const cdsl_ai_config_t* config,
			       cdsl_ai_stream_cb_t callback,
			       void* user_data);

/**
 * @brief Stream review a DSL rule.
 *
 * Sends the review request to the LLM API with streaming enabled.
 *
 * @param dsl_code DSL rule to review
 * @param schema Registered schema
 * @param config AI configuration
 * @param callback Streaming callback function
 * @param user_data User data passed to callback
 * @return Complete response string (must be freed with free()), or NULL on error
 */
char* cdsl_ai_review_stream(const char* dsl_code,
			    const cdsl_schema_t* schema,
			    const cdsl_ai_config_t* config,
			    cdsl_ai_stream_cb_t callback,
			    void* user_data);

#endif
/** @} */
