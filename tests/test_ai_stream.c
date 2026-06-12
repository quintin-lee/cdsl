#include "test.h"
#include "cdsl/ai.h"
#include <stdlib.h>
#include <string.h>

static int stream_cb_called = 0;
static char* stream_cb_result = NULL;

static void
stream_callback(const char* chunk, void* user_data)
{
	(void)user_data;
	stream_cb_called = 1;
	if (chunk) {
		free(stream_cb_result);
		stream_cb_result = strdup(chunk);
	}
}

void
test_ai_translate_stream_mock(void)
{
	TEST_BEGIN("cdsl_ai_translate_stream mock mode");

	stream_cb_called = 0;
	free(stream_cb_result);
	stream_cb_result = NULL;

	cdsl_ai_config_t cfg = cdsl_ai_config_default();
	cfg.use_mock = 1;

	char* result = cdsl_ai_translate_stream(
	    "supplier credit check > 1000", NULL, &cfg, stream_callback, NULL);
	TEST_ASSERT_NOT_NULL(result, "mock translate_stream should return result");
	TEST_ASSERT(stream_cb_called == 1, "stream callback should be invoked");
	TEST_ASSERT_NOT_NULL(stream_cb_result, "callback should receive data");
	TEST_ASSERT(strstr(result, "RULE") != NULL, "result should contain DSL rule");

	free(result);
	free(stream_cb_result);
	stream_cb_result = NULL;

	TEST_END();
}

void
test_ai_review_stream_mock(void)
{
	TEST_BEGIN("cdsl_ai_review_stream mock mode");

	stream_cb_called = 0;
	free(stream_cb_result);
	stream_cb_result = NULL;

	cdsl_ai_config_t cfg = cdsl_ai_config_default();
	cfg.use_mock = 1;

	char* result = cdsl_ai_review_stream(
	    "RULE test { WHEN true THEN block() }", NULL, &cfg, stream_callback, NULL);
	TEST_ASSERT_NOT_NULL(result, "mock review_stream should return result");
	TEST_ASSERT(stream_cb_called == 1, "stream callback should be invoked");

	free(result);
	free(stream_cb_result);
	stream_cb_result = NULL;

	TEST_END();
}

void
test_ai_translate_stream_null_input(void)
{
	TEST_BEGIN("cdsl_ai_translate_stream null input");

	cdsl_ai_config_t cfg = cdsl_ai_config_default();
	cfg.use_mock = 1;

	char* result = cdsl_ai_translate_stream(NULL, NULL, &cfg, stream_callback, NULL);
	TEST_ASSERT_NULL(result, "null input should return NULL");

	TEST_END();
}

int
main(void)
{
	printf("========================================\n");
	printf("  AI Stream Tests\n");
	printf("========================================\n");

	test_ai_translate_stream_mock();
	test_ai_review_stream_mock();
	test_ai_translate_stream_null_input();

	TEST_SUMMARY();
	TEST_EXIT();
}
