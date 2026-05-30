/**
 * @file test_ai_cache.c
 * @brief Unit tests for the AI bridge cache injection mechanism.
 *
 * Tests that a user-provided cache implementation is correctly
 * invoked during AI translation calls.
 */

#include "test.h"
#include "ai_bridge.h"
#include "cdsl/util/hashmap.h"
#include <stdlib.h>
#include <string.h>

/** @brief Counter for cache get invocations. */
static int get_count = 0;
/** @brief Counter for cache put invocations. */
static int put_count = 0;

/**
 * @brief Mock cache get: wraps hashmap lookup.
 * @param ctx Opaque hashmap pointer
 * @param key Cache key
 * @return Cached string or NULL
 */
static char*
my_cache_get(void* ctx, const char* key)
{
	(void)key;
	get_count++;
	return cdsl_hashmap_get((cdsl_hashmap_t*)ctx, key);
}

/**
 * @brief Mock cache put: wraps hashmap insertion.
 * @param ctx Opaque hashmap pointer
 * @param key Cache key
 * @param value Value to cache
 */
static void
my_cache_put(void* ctx, const char* key, const char* value)
{
	(void)key;
	put_count++;
	cdsl_hashmap_put((cdsl_hashmap_t*)ctx, key, strdup(value));
}

/**
 * @brief Test that the AI bridge calls cache get/put methods.
 */
void
test_ai_cache_injection(void)
{
	TEST_BEGIN("AI cache injection");

	cdsl_hashmap_t* map = cdsl_hashmap_create(10);
	cdsl_ai_cache_t cache = {
	    .ctx = map,
	    .get = my_cache_get,
	    .put = my_cache_put,
	};

	cdsl_ai_config_t cfg = cdsl_ai_config_default();
	cfg.use_mock = 0;
	cfg.api_key = "fake_key";
	cfg.api_base = "http://localhost:12345";
	cfg.model = "fake_model";
	cfg.cache = &cache;

	/* This call will trigger call_llm_api, which will check the cache */
	get_count = 0;
	char* result = cdsl_ai_translate("test rule", NULL, &cfg);
	TEST_ASSERT(get_count > 0, "cache get should be called at least once");

	/* Clean up result if it was mock-generated as fallback */
	if (result) {
		free(result);
	}

	cdsl_hashmap_free(map, free);
	TEST_END();
}

/**
 * @brief Main entry: run all AI cache test cases.
 * @return 0 if all tests passed, 1 otherwise
 */
int
main(void)
{
	printf("========================================\n");
	printf("  AI Cache Tests\n");
	printf("========================================\n");

	test_ai_cache_injection();

	TEST_SUMMARY();
	TEST_EXIT();
}
