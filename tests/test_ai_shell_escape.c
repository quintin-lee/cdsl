/**
 * @file test_ai_shell_escape.c
 * @brief Security unit tests for AI bridge POSIX single-quote shell escaping.
 */

#include "test.h"
#include "../src/ai/internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
test_escape_sq_unit(void)
{
	TEST_BEGIN("ai: escape_sq unit cases");

	TEST_ASSERT_NULL(cdsl_ai_escape_shell_sq_internal(NULL), "NULL returns NULL");

	char* empty = cdsl_ai_escape_shell_sq_internal("");
	TEST_ASSERT_NOT_NULL(empty, "empty string not NULL");
	TEST_ASSERT_STR(empty, "", "empty string unchanged");
	free(empty);

	char* plain = cdsl_ai_escape_shell_sq_internal("hello $(whoami) `id` \"$HOME\"");
	TEST_ASSERT_NOT_NULL(plain, "no-single-quote string not NULL");
	TEST_ASSERT_STR(
	    plain, "hello $(whoami) `id` \"$HOME\"", "non-single-quote chars unchanged");
	free(plain);

	char* single = cdsl_ai_escape_shell_sq_internal("it's");
	TEST_ASSERT_NOT_NULL(single, "single-quote string not NULL");
	TEST_ASSERT_STR(single, "it'\"'\"'s", "single quote replaced with '\"'\"'");
	free(single);

	char* multi = cdsl_ai_escape_shell_sq_internal("''a'");
	TEST_ASSERT_NOT_NULL(multi, "multiple single quotes not NULL");
	TEST_ASSERT_STR(multi, "'\"'\"''\"'\"'a'\"'\"'", "multiple single quotes escaped");
	free(multi);

	TEST_END();
}

#ifndef _WIN32
static void
assert_shell_roundtrip(const char* input)
{
	char* esc = cdsl_ai_escape_shell_sq_internal(input);
	TEST_ASSERT_NOT_NULL(esc, "escaped string not NULL");
	if (!esc) {
		return;
	}

	size_t cmd_len = strlen(esc) + 32;
	char* cmd = malloc(cmd_len);
	TEST_ASSERT_NOT_NULL(cmd, "command alloc");
	if (!cmd) {
		free(esc);
		return;
	}
	snprintf(cmd, cmd_len, "printf '%%s' '%s'", esc);
	free(esc);

	FILE* fp = popen(cmd, "r");
	free(cmd);
	TEST_ASSERT_NOT_NULL(fp, "popen succeeded");
	if (!fp) {
		return;
	}

	char buf[1024];
	size_t n = fread(buf, 1, sizeof(buf) - 1, fp);
	buf[n] = '\0';
	int rc = pclose(fp);
	TEST_ASSERT_INT(rc, 0, "sh exited with 0");
	TEST_ASSERT_STR(buf, input, "shell output matches input byte-for-byte");
}

static void
test_escape_sq_posix_shell_roundtrip(void)
{
	TEST_BEGIN("ai: POSIX sh roundtrip against command injection payloads");

	assert_shell_roundtrip("it's a test");
	assert_shell_roundtrip("it's\"; echo INJECTED; echo \"");
	assert_shell_roundtrip("quote' $(echo INJECTED) `echo INJECTED` '$HOME' \\\"end");
	assert_shell_roundtrip("'''leading and trailing'''");

	TEST_END();
}
#endif

int
main(void)
{
	printf("Running AI shell escape tests...\n");
	test_escape_sq_unit();
#ifndef _WIN32
	test_escape_sq_posix_shell_roundtrip();
#endif
	TEST_SUMMARY();
	TEST_EXIT();
}
