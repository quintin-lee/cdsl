/**
 * @file test_validation_errors.c
 * @brief Validation error location tests for C-DSL (Subproject D2).
 *
 * Verifies that schema verification errors carry source line/column
 * information from AST node positions.
 */

#include <cdsl/api.h>
#include <cdsl/util/error.h>
#include <string.h>
#include "test.h"

static void
test_validation_error_has_location(void)
{
	TEST_BEGIN("validation error has line/column");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "age", CDSL_TYPE_INT);

	/* Rule references unknown variable "salary" — should produce error with line/column */
	const char* dsl = "RULE check_rule {\n"
			  "  WHEN salary > 0 THEN block()\n"
			  "}";
	cdsl_error_list_t* parse_errors = NULL;
	cdsl_rule_t* rule = cdsl_parse_string(dsl, &parse_errors);
	if (parse_errors) {
		cdsl_error_list_free(parse_errors);
	}
	TEST_ASSERT_NOT_NULL(rule, "rule should parse");

	cdsl_error_list_t* errors = cdsl_verify_rule_detailed(rule, schema);
	TEST_ASSERT_NOT_NULL(errors, "errors should not be NULL");
	TEST_ASSERT(errors->count > 0, "should have at least one error");

	cdsl_error_t* err = errors->errors[0];
	TEST_ASSERT_INT(err->line, 2, "error line should be 2");
	TEST_ASSERT_INT(err->column, 8, "error column should be 8");
	TEST_ASSERT(strstr(err->message, "salary") != NULL, "message should mention salary");

	cdsl_error_list_free(errors);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);

	TEST_END();
}

static void
test_validation_error_null_input(void)
{
	TEST_BEGIN("validation error for null input");

	cdsl_error_list_t* errors = cdsl_verify_rule_detailed(NULL, NULL);
	TEST_ASSERT_NOT_NULL(errors, "errors should not be NULL for null input");
	TEST_ASSERT(errors->count > 0, "should have error for null rule/schema");
	cdsl_error_list_free(errors);

	TEST_END();
}

int
main(void)
{
	printf("Running validation error location tests...\n");

	test_validation_error_has_location();
	test_validation_error_null_input();

	TEST_SUMMARY();
	TEST_EXIT();
}
