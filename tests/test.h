#ifndef CDSL_TEST_H
#define CDSL_TEST_H

#include <stdio.h>
#include <string.h>

static int _test_pass = 0;
static int _test_fail = 0;

#define TEST_ASSERT(cond, msg)                                                                     \
	do {                                                                                       \
		if (cond) {                                                                        \
			_test_pass++;                                                              \
		} else {                                                                           \
			_test_fail++;                                                              \
			fprintf(stderr, "  FAIL: %s (line %d)\n", msg, __LINE__);                  \
		}                                                                                  \
	} while (0)

#define TEST_ASSERT_STR(a, b, msg) TEST_ASSERT(strcmp(a, b) == 0, msg)
#define TEST_ASSERT_INT(a, b, msg) TEST_ASSERT((a) == (b), msg)
#define TEST_ASSERT_NULL(p, msg) TEST_ASSERT((p) == NULL, msg)
#define TEST_ASSERT_NOT_NULL(p, msg) TEST_ASSERT((p) != NULL, msg)

#define TEST_BEGIN(name)                                                                           \
	do {                                                                                       \
		printf("  [TEST] %s ... ", name);                                                  \
	} while (0)

#define TEST_END()                                                                                 \
	do {                                                                                       \
		printf("OK\n");                                                                    \
	} while (0)

#define TEST_SUMMARY()                                                                             \
	do {                                                                                       \
		printf("\n========================================\n");                            \
		printf("  TEST RESULTS: %d passed, %d failed\n", _test_pass, _test_fail);          \
		printf("========================================\n");                              \
	} while (0)

#define TEST_EXIT() return _test_fail > 0 ? 1 : 0

#endif
