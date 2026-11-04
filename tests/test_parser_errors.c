/**
 * @file test_parser_errors.c
 * @brief Parser error recovery tests for C-DSL.
 *
 * Tests that the parser gracefully recovers from syntax errors,
 * reports structured errors, and supports partial results when parsing
 * multiple rules.
 *
 * Note: line/column from parser-level yyerror may be unreliable during
 * Bison error recovery (scanner state can be corrupted). Lexer-level
 * errors (unknown characters) do report accurate line/column.
 */

#include <cdsl/ast.h>
#include <cdsl/util/error.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "test.h"

#define TEST_PARSE_BEGIN(name) TEST_BEGIN(name)

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

/* Test 1: Missing THEN — parser reports error, single rule returns NULL */
static void
test_parser_error_missing_then(void)
{
	TEST_PARSE_BEGIN("Parser error: missing THEN keyword");

	cdsl_error_list_t* errs = NULL;
	const char* dsl = "RULE r1 { WHEN user.age > 18 block(\"adult\") }";

	cdsl_rule_t* rule = cdsl_parse_string(dsl, &errs);

	TEST_ASSERT_NULL(rule, "Single failing rule returns NULL");
	TEST_ASSERT_NOT_NULL(errs, "Error list should be created");
	TEST_ASSERT_INT(errs->count >= 1, 1, "Should have at least 1 error");
	TEST_ASSERT(errs->errors[0]->kind == CDSL_ERR_SYNTAX,
		    "Error kind should be CDSL_ERR_SYNTAX");
	TEST_ASSERT(errs->errors[0]->message != NULL, "Error message should be set");

	cleanup_parse(rule, errs);
	TEST_END();
}

/* Test 2: Extra closing brace — first valid rule is preserved */
static void
test_parser_error_extra_brace(void)
{
	TEST_PARSE_BEGIN("Parser error: extra closing brace");

	cdsl_error_list_t* errs = NULL;
	const char* dsl = "RULE r1 { WHEN true THEN block(\"x\") } }";

	cdsl_rule_t* rule = cdsl_parse_string(dsl, &errs);

	/* First valid rule is kept even though second '}' is an error */
	TEST_ASSERT_NOT_NULL(rule, "First rule should be preserved");
	TEST_ASSERT_STR(rule->name, "r1", "Should be the first rule");
	TEST_ASSERT_NOT_NULL(errs, "Error list should be created");
	TEST_ASSERT_INT(errs->count >= 1, 1, "Should have at least 1 error");

	cleanup_parse(rule, errs);
	TEST_END();
}

/* Test 3: Invalid operator in expression — error node, rule still created */
static void
test_parser_error_bad_operator(void)
{
	TEST_PARSE_BEGIN("Parser error: invalid operator in expression");

	cdsl_error_list_t* errs = NULL;
	const char* dsl = "RULE r1 { WHEN x @@ 5 THEN block(\"y\") }";

	cdsl_rule_t* rule = cdsl_parse_string(dsl, &errs);

	/* Expression error creates error node; rule_declaration still succeeds */
	TEST_ASSERT_NOT_NULL(rule, "Rule should be created despite expression error");
	TEST_ASSERT_NOT_NULL(errs, "Error list should be created");
	TEST_ASSERT_INT(errs->count >= 1, 1, "Should have at least 1 error");

	cleanup_parse(rule, errs);
	TEST_END();
}

/* Test 4: Multi-rule with one bad — returns last parsed rule */
static void
test_parser_multi_rule_partial(void)
{
	TEST_PARSE_BEGIN("Multi-rule: returns last valid rule");

	cdsl_error_list_t* errs = NULL;
	const char* dsl = "RULE r1 { WHEN true THEN block(\"a\") } "
			  "RULE r2 { WHEN x @@ 5 THEN block(\"b\") }";

	cdsl_rule_t* rule = cdsl_parse_string(dsl, &errs);

	TEST_ASSERT_NOT_NULL(rule, "A rule should be returned");
	TEST_ASSERT_STR(rule->name, "r2", "Should be the last parsed rule");
	TEST_ASSERT_NOT_NULL(errs, "Error list should be created");
	TEST_ASSERT_INT(errs->count >= 1, 1, "Should have at least 1 error");

	cleanup_parse(rule, errs);
	TEST_END();
}

/* Test 5: Error has non-negative line and column */
static void
test_parser_error_location(void)
{
	TEST_PARSE_BEGIN("Parser error: line and column are non-negative");

	cdsl_error_list_t* errs = NULL;
	const char* dsl = "RULE r1 { WHEN x @@ 5 THEN block(\"y\") }";

	cdsl_rule_t* rule = cdsl_parse_string(dsl, &errs);

	TEST_ASSERT_NOT_NULL(errs, "Error list should be created");
	TEST_ASSERT_INT(errs->count >= 1, 1, "Should have at least 1 error");
	TEST_ASSERT(errs->errors[0]->line >= 0, "Line should be >= 0");
	TEST_ASSERT(errs->errors[0]->column >= 0, "Column should be >= 0");

	cleanup_parse(rule, errs);
	TEST_END();
}

/* Test 6: Error message is populated */
static void
test_parser_error_message(void)
{
	TEST_PARSE_BEGIN("Parser error: message is populated");

	cdsl_error_list_t* errs = NULL;
	const char* dsl = "RULE r1 { WHEN user.age > 18 block(\"adult\") }";

	cdsl_rule_t* rule = cdsl_parse_string(dsl, &errs);

	TEST_ASSERT_NOT_NULL(errs, "Error list should be created");
	TEST_ASSERT_INT(errs->count >= 1, 1, "Should have at least 1 error");
	TEST_ASSERT(errs->errors[0]->message != NULL, "Error message should be set");
	TEST_ASSERT(errs->errors[0]->message && strlen(errs->errors[0]->message) > 0,
		    "Error message should be non-empty");

	cleanup_parse(rule, errs);
	TEST_END();
}

/* Test 7: NULL error list still fails fast (backward compatibility) */
static void
test_parser_error_null_list_fail_fast(void)
{
	TEST_PARSE_BEGIN("NULL error list: fail fast on syntax error");

	const char* dsl = "RULE r1 { WHEN x @@ 5 THEN block(\"y\") }";

	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);

	TEST_ASSERT_NULL(rule, "Should return NULL when errors is NULL and parse fails");

	TEST_END();
}

/* Test 8: Valid rule still works with error list */
static void
test_parser_valid_rule_with_errors(void)
{
	TEST_PARSE_BEGIN("Valid rule with error list: no errors");

	cdsl_error_list_t* errs = NULL;
	const char* dsl = "RULE r1 { WHEN true THEN block(\"a\") }";

	cdsl_rule_t* rule = cdsl_parse_string(dsl, &errs);

	TEST_ASSERT_NOT_NULL(rule, "Valid rule should be returned");
	TEST_ASSERT_NOT_NULL(errs, "Error list should be created");
	TEST_ASSERT_INT(errs->count, 0, "Should have no errors for valid rule");
	TEST_ASSERT_STR(rule->name, "r1", "Rule name should match");

	cleanup_parse(rule, errs);
	TEST_END();
}

int
main(void)
{
	printf("Running parser error recovery tests...\n");
	test_parser_error_missing_then();
	test_parser_error_extra_brace();
	test_parser_error_bad_operator();
	test_parser_multi_rule_partial();
	test_parser_error_location();
	test_parser_error_message();
	test_parser_error_null_list_fail_fast();
	test_parser_valid_rule_with_errors();
	TEST_SUMMARY();
	TEST_EXIT();
}
