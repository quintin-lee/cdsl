/**
 * @file test_arena_ownership.c
 * @brief Ownership contract tests for cdsl_parse_string's errors path.
 *
 * Verifies that when a partial rule survives a parse error, the parse-time
 * arena is attached to rule->arena so cdsl_free_rule() reclaims it safely
 * (no leak under ASan), and that a total parse failure returns NULL with no
 * rule for the caller to free.
 */

#include <cdsl/ast.h>
#include <cdsl/util/error.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "test.h"

static void
cleanup_parse(cdsl_rule_t* rule, cdsl_error_list_t* errs)
{
	if (rule) {
		cdsl_free_rule(rule);
	}
	if (errs) {
		cdsl_error_list_free(errs);
	}
}

/* Partial rule (first rule valid, trailing token errors) survives the errors
 * path: it must carry ownership of its arena so cdsl_free_rule reclaims it. */
static void
test_partial_rule_owns_arena(void)
{
	TEST_BEGIN("Partial rule on errors path carries arena ownership");

	cdsl_error_list_t* errs = NULL;
	const char* dsl = "RULE r1 { WHEN true THEN block(\"x\") } }";

	cdsl_rule_t* rule = cdsl_parse_string(dsl, &errs);
	TEST_ASSERT_NOT_NULL(rule, "First rule should be preserved");
	TEST_ASSERT_NOT_NULL(errs, "Error list should be created");
	TEST_ASSERT_INT(errs->count >= 1, 1, "Should have at least 1 error");
	/* The partial rule owns the parse arena: cdsl_free_rule reclaims it.
	 * Under ASan this is the regression guard against the old leak. */
	cdsl_free_rule(rule);
	cdsl_error_list_free(errs);

	TEST_END();
}

/* Expression error: rule_declaration still succeeds (error node embedded),
 * so the rule survives and must own its arena. */
static void
test_expr_error_rule_owns_arena(void)
{
	TEST_BEGIN("Rule with embedded expression error owns arena");

	cdsl_error_list_t* errs = NULL;
	const char* dsl = "RULE r1 { WHEN x @@ 5 THEN block(\"y\") }";

	cdsl_rule_t* rule = cdsl_parse_string(dsl, &errs);
	TEST_ASSERT_NOT_NULL(rule, "Rule created despite expression error");
	TEST_ASSERT_NOT_NULL(errs, "Error list should be created");
	TEST_ASSERT_INT(errs->count >= 1, 1, "Should have at least 1 error");

	cleanup_parse(rule, errs);

	TEST_END();
}

/* Total parse failure returns NULL; there is no rule for the caller to free,
 * and the arena is reclaimed internally (no leak). */
static void
test_total_failure_returns_null(void)
{
	TEST_BEGIN("Total parse failure returns NULL (no rule to free)");

	cdsl_error_list_t* errs = NULL;
	const char* dsl = "RULE r1 { WHEN user.age > 18 block(\"adult\") }";

	cdsl_rule_t* rule = cdsl_parse_string(dsl, &errs);
	TEST_ASSERT_NULL(rule, "Single failing rule returns NULL");
	TEST_ASSERT_NOT_NULL(errs, "Error list should be created");
	TEST_ASSERT_INT(errs->count >= 1, 1, "Should have at least 1 error");

	cdsl_error_list_free(errs);

	TEST_END();
}

/* The success path (valid rule, error list requested) still attaches the
 * arena to the rule so cdsl_free_rule reclaims it; mirrors manager/canary
 * callers that request an error list on the happy path. */
static void
test_valid_rule_owns_arena(void)
{
	TEST_BEGIN("Valid rule with error list owns arena");

	cdsl_error_list_t* errs = NULL;
	const char* dsl = "RULE r1 { WHEN true THEN block(\"a\") }";

	cdsl_rule_t* rule = cdsl_parse_string(dsl, &errs);
	TEST_ASSERT_NOT_NULL(rule, "Valid rule should be returned");
	TEST_ASSERT_NOT_NULL(errs, "Error list should be created");
	TEST_ASSERT_INT(errs->count, 0, "No errors for valid rule");
	TEST_ASSERT_STR(rule->name, "r1", "Rule name should match");

	cleanup_parse(rule, errs);

	TEST_END();
}

int
main(void)
{
	printf("Running parse arena ownership tests...\n");
	test_partial_rule_owns_arena();
	test_expr_error_rule_owns_arena();
	test_total_failure_returns_null();
	test_valid_rule_owns_arena();
	TEST_SUMMARY();
	TEST_EXIT();
}
