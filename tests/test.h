/**
 * @file test.h
 * @brief Minimal test framework header for C-DSL unit tests.
 *
 * Provides lightweight assertion macros and test harness utilities
 * without external dependencies. Tracks pass/fail counts globally
 * and prints formatted test results.
 *
 * Macros:
 * - TEST_BEGIN(name): Prints test name
 * - TEST_END(): Prints OK/FAILED based on accumulated failures
 * - TEST_ASSERT(cond, msg): Generic boolean assertion
 * - TEST_ASSERT_STR(a, b, msg): String equality assertion
 * - TEST_ASSERT_INT(a, b, msg): Integer equality assertion
 * - TEST_ASSERT_NULL/NOT_NULL(p, msg): Null pointer assertions
 * - TEST_SUMMARY(): Print final summary of all tests
 * - TEST_EXIT(): Return appropriate exit code
 */

#ifndef CDSL_TEST_H
#define CDSL_TEST_H

#include <stdio.h>
#include <string.h>

/** @brief Global test pass counter. */
static int _test_pass = 0;
/** @brief Global test failure counter. */
static int _test_fail = 0;

/**
 * @brief Assert that a condition is true.
 * @param cond Condition to evaluate
 * @param msg Description of the assertion (printed on failure)
 */
#define TEST_ASSERT(cond, msg)                                                                     \
	do {                                                                                       \
		if (cond) {                                                                        \
			_test_pass++;                                                              \
		} else {                                                                           \
			_test_fail++;                                                              \
			fprintf(stderr, "  FAIL: %s (line %d)\n", msg, __LINE__);                  \
		}                                                                                  \
	} while (0)

/**
 * @brief Assert string equality using strcmp.
 */
#define TEST_ASSERT_STR(a, b, msg) TEST_ASSERT(strcmp(a, b) == 0, msg)

/**
 * @brief Assert integer equality.
 */
#define TEST_ASSERT_INT(a, b, msg) TEST_ASSERT((a) == (b), msg)

/**
 * @brief Assert that a pointer is NULL.
 */
#define TEST_ASSERT_NULL(p, msg) TEST_ASSERT((p) == NULL, msg)

/**
 * @brief Assert that a pointer is not NULL.
 */
#define TEST_ASSERT_NOT_NULL(p, msg) TEST_ASSERT((p) != NULL, msg)

/**
 * @brief Begin a named test case.
 * @param name Test name string to display
 */
#define TEST_BEGIN(name)                                                                           \
	do {                                                                                       \
		printf("  [TEST] %s ... ", name);                                                  \
	} while (0)

/**
 * @brief End a test case and print pass/fail status.
 */
#define TEST_END()                                                                                 \
	do {                                                                                       \
		if (_test_fail == 0) {                                                             \
			printf("OK\n");                                                            \
		} else {                                                                           \
			printf("FAILED\n");                                                        \
		}                                                                                  \
	} while (0)

/**
 * @brief Print summary of all test results.
 */
#define TEST_SUMMARY()                                                                             \
	do {                                                                                       \
		printf("\n========================================\n");                            \
		printf("  TEST RESULTS: %d passed, %d failed\n", _test_pass, _test_fail);          \
		printf("========================================\n");                              \
	} while (0)

/**
 * @brief Return appropriate exit code (1 if any tests failed).
 */
#define TEST_EXIT() return _test_fail > 0 ? 1 : 0

#endif
