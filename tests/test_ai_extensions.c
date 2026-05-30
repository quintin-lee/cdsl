/**
 * @file test_ai_extensions.c
 * @brief Unit tests for the AI provider and cache driver registries.
 *
 * Tests that custom AI providers and cache drivers can be registered
 * and dispatched correctly through the AI bridge layer.
 */

#include "test.h"
#include "cdsl/ai.h"
#include "cdsl/util/hashmap.h"
#include <stdlib.h>
#include <string.h>

/** @brief Whether the mock provider's translate was called. */
static int provider_called = 0;
/** @brief Whether the mock cache driver's get was called. */
static int cache_driver_called = 0;

/**
 * @brief Mock provider translate: returns a fixed DSL string.
 */
static char*
mock_provider_translate(void* ctx,
			const char* nl,
			const cdsl_schema_t* schema,
			const cdsl_ai_config_t* cfg)
{
	(void)ctx;
	(void)nl;
	(void)schema;
	(void)cfg;
	provider_called = 1;
	return strdup("RULE mock_provider { WHEN true THEN block() }");
}

/**
 * @brief Mock provider review: returns an approved review.
 */
static cdsl_ai_review_t*
mock_provider_review(void* ctx,
		     const char* dsl,
		     const cdsl_schema_t* schema,
		     const cdsl_ai_config_t* cfg)
{
	(void)ctx;
	(void)dsl;
	(void)schema;
	(void)cfg;
	cdsl_ai_review_t* rev = calloc(1, sizeof(*rev));
	rev->approved = 1;
	rev->reason = strdup("mock approved");
	rev->suggestions = strdup("");
	return rev;
}

/**
 * @brief Mock cache driver get: returns NULL (cache miss).
 */
static char*
mock_cache_get(void* ctx, const char* key)
{
	(void)ctx;
	(void)key;
	cache_driver_called = 1;
	return NULL;
}

/**
 * @brief Mock cache driver put: no-op.
 */
static void
mock_cache_put(void* ctx, const char* key, const char* value)
{
	(void)ctx;
	(void)key;
	(void)value;
}

/**
 * @brief Test AI extension registry: provider and cache driver dispatch.
 */
void
test_ai_extension_system(void)
{
	TEST_BEGIN("AI extension registry system");

	/* 1. Register Mock Provider */
	cdsl_ai_provider_t prov = {.ctx = NULL,
				   .translate = mock_provider_translate,
				   .review = mock_provider_review,
				   .translate_stream = NULL,
				   .review_stream = NULL};
	cdsl_ai_register_provider("mock_prov", &prov);

	/* 2. Register Mock Cache Driver */
	cdsl_ai_cache_t cache = {.ctx = NULL, .get = mock_cache_get, .put = mock_cache_put};
	cdsl_ai_register_cache_driver("mock_cache", &cache);

	/* 3. Test Provider Dispatch */
	cdsl_ai_config_t cfg = cdsl_ai_config_default();
	cfg.provider_name = "mock_prov";
	cfg.use_mock = 0;

	provider_called = 0;
	char* res = cdsl_ai_translate("something", NULL, &cfg);
	TEST_ASSERT(provider_called == 1, "custom provider should be called");
	TEST_ASSERT_NOT_NULL(res, "result should not be null");
	TEST_ASSERT(strstr(res, "mock_provider") != NULL, "result should come from mock provider");
	free(res);

	/* 4. Test Cache Driver Dispatch */
	cfg.provider_name = "default"; /* Use default provider to trigger call_llm_api */
	cfg.cache_driver_name = "mock_cache";
	cfg.api_key = "dummy";
	cfg.api_base = "dummy";
	cfg.model = "dummy";

	cache_driver_called = 0;
	/* This will call call_llm_api -> get_cache_driver -> mock_cache_get */
	res = cdsl_ai_translate("something", NULL, &cfg);
	TEST_ASSERT(cache_driver_called == 1, "custom cache driver should be called");
	if (res) {
		free(res);
	}

	TEST_END();
}

/**
 * @brief Main entry: run all AI extension test cases.
 * @return 0 if all tests passed, 1 otherwise
 */
int
main(void)
{
	printf("========================================\n");
	printf("  AI Extension System Tests\n");
	printf("========================================\n");

	test_ai_extension_system();

	TEST_SUMMARY();
	TEST_EXIT();
}
