/**
 * @file test_error.c
 * @brief Comprehensive error handling tests for C-DSL.
 *
 * Tests error creation, error list management, error types,
 * error hints, and error reporting functionality.
 */

#include <cdsl/util/error.h>
#include <cdsl/schema.h>
#include <cdsl/ast.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "test.h"

// Test helper macros
#define TEST_ERROR_BEGIN(name) TEST_BEGIN(name)
#define TEST_ERROR_END() TEST_END()

// Test 1: Basic error creation and management
static void
test_error_creation()
{
	TEST_ERROR_BEGIN("Basic error creation");

	// Create different types of errors
	cdsl_error_t* syntax_err = cdsl_error_create(
	    CDSL_ERR_SYNTAX, 10, 5, "Missing semicolon", "Add ';' at end of line");
	cdsl_error_t* type_err =
	    cdsl_error_create(CDSL_ERR_TYPE, 15, 8, "Type mismatch", "Expected int, got string");
	cdsl_error_t* semantic_err = cdsl_error_create(
	    CDSL_ERR_SEMANTIC, 20, 3, "Undefined variable", "Variable 'x' not declared");
	cdsl_error_t* runtime_err = cdsl_error_create(
	    CDSL_ERR_RUNTIME, 25, 1, "Division by zero", "Check denominator before division");

	TEST_ASSERT_NOT_NULL(syntax_err, "Syntax error should be created");
	TEST_ASSERT_NOT_NULL(type_err, "Type error should be created");
	TEST_ASSERT_NOT_NULL(semantic_err, "Semantic error should be created");
	TEST_ASSERT_NOT_NULL(runtime_err, "Runtime error should be created");

	// Verify error properties
	TEST_ASSERT(syntax_err->kind == CDSL_ERR_SYNTAX, "Error kind should be CDSL_ERR_SYNTAX");
	TEST_ASSERT(syntax_err->line == 10, "Error line should be 10");
	TEST_ASSERT(syntax_err->column == 5, "Error column should be 5");
	TEST_ASSERT(strcmp(syntax_err->message, "Missing semicolon") == 0,
		    "Error message should match");
	TEST_ASSERT(strcmp(syntax_err->hint, "Add ';' at end of line") == 0,
		    "Error hint should match");

	// Test error printing
	printf("  [DEBUG] Testing error printing:\n");
	cdsl_error_print(syntax_err);
	cdsl_error_print(type_err);
	cdsl_error_print(semantic_err);
	cdsl_error_print(runtime_err);

	// Free errors
	cdsl_error_free(syntax_err);
	cdsl_error_free(type_err);
	cdsl_error_free(semantic_err);
	cdsl_error_free(runtime_err);

	TEST_ERROR_END();
}

// Test 2: Error list management
static void
test_error_list_management()
{
	TEST_ERROR_BEGIN("Error list management");

	cdsl_error_list_t* error_list = cdsl_error_list_create();
	TEST_ASSERT_NOT_NULL(error_list, "Error list should be created");
	TEST_ASSERT(error_list->count == 0, "New error list should be empty");
	TEST_ASSERT(error_list->capacity >= 16, "Error list should have initial capacity");

	// Add multiple errors
	cdsl_error_t* err1 = cdsl_error_create(CDSL_ERR_SYNTAX, 10, 5, "Error 1", "Hint 1");
	cdsl_error_t* err2 = cdsl_error_create(CDSL_ERR_TYPE, 15, 8, "Error 2", "Hint 2");
	cdsl_error_t* err3 = cdsl_error_create(CDSL_ERR_SEMANTIC, 20, 3, "Error 3", "Hint 3");

	cdsl_error_list_add(error_list, err1);
	cdsl_error_list_add(error_list, err2);
	cdsl_error_list_add(error_list, err3);

	TEST_ASSERT(error_list->count == 3, "Error list should contain 3 errors");
	TEST_ASSERT(error_list->errors[0] == err1, "First error should be err1");
	TEST_ASSERT(error_list->errors[1] == err2, "Second error should be err2");
	TEST_ASSERT(error_list->errors[2] == err3, "Third error should be err3");

	// Test error list printing
	printf("  [DEBUG] Testing error list printing:\n");
	cdsl_error_list_print(error_list);

	// Test error list has errors
	TEST_ASSERT(cdsl_error_list_has_errors(error_list), "Error list should have errors");

	// Free error list (this also frees contained errors)
	cdsl_error_list_free(error_list);

	TEST_ERROR_END();
}

// Test 3: Error list capacity growth
static void
test_error_list_capacity_growth()
{
	TEST_ERROR_BEGIN("Error list capacity growth");

	cdsl_error_list_t* error_list = cdsl_error_list_create();

	// Add many errors to trigger capacity growth
	for (int i = 0; i < 50; i++) {
		char msg[32];
		char hint[32];
		sprintf(msg, "Error %d", i);
		sprintf(hint, "Hint %d", i);

		cdsl_error_t* err = cdsl_error_create(CDSL_ERR_SYNTAX, i, 1, msg, hint);
		cdsl_error_list_add(error_list, err);
	}

	TEST_ASSERT(error_list->count == 50, "Error list should contain 50 errors");
	TEST_ASSERT(error_list->capacity >= 50, "Error list capacity should be sufficient");

	// Verify all errors are accessible
	for (int i = 0; i < 50; i++) {
		char expected[32];
		sprintf(expected, "Error %d", i);
		TEST_ASSERT(error_list->errors[i] != NULL, "Error should not be NULL");
		TEST_ASSERT(strcmp(error_list->errors[i]->message, expected) == 0,
			    "Error message should match");
	}

	cdsl_error_list_free(error_list);
	TEST_ERROR_END();
}

// Test 4: Error type validation
static void
test_error_type_validation()
{
	TEST_ERROR_BEGIN("Error type validation");

	// Test all error types
	cdsl_error_t* errors[4];
	const char* type_names[] = {"SYNTAX", "TYPE", "SEMANTIC", "RUNTIME"};

	for (int i = 0; i < 4; i++) {
		cdsl_error_kind_t kind = (cdsl_error_kind_t)i;
		char msg[32];
		sprintf(msg, "%s error", type_names[i]);

		errors[i] = cdsl_error_create(kind, 10 + i, 5 + i, msg, NULL);
		TEST_ASSERT_NOT_NULL(errors[i], "Error should be created");
		TEST_ASSERT(errors[i]->kind == kind, "Error kind should match");
	}

	// Test error printing with different types
	printf("  [DEBUG] Testing different error types:\n");
	for (int i = 0; i < 4; i++) {
		cdsl_error_print(errors[i]);
	}

	// Free all errors
	for (int i = 0; i < 4; i++) {
		cdsl_error_free(errors[i]);
	}

	TEST_ERROR_END();
}

// Test 5: Error with NULL hint
static void
test_error_null_hint()
{
	TEST_ERROR_BEGIN("Error with NULL hint");

	cdsl_error_t* err = cdsl_error_create(CDSL_ERR_SYNTAX, 10, 5, "Error without hint", NULL);
	TEST_ASSERT_NOT_NULL(err, "Error should be created");
	TEST_ASSERT(err->hint == NULL, "Hint should be NULL");

	cdsl_error_print(err);
	cdsl_error_free(err);

	TEST_ERROR_END();
}

// Test 6: Empty error list handling
static void
test_empty_error_list()
{
	TEST_ERROR_BEGIN("Empty error list handling");

	cdsl_error_list_t* error_list = cdsl_error_list_create();
	TEST_ASSERT_NOT_NULL(error_list, "Error list should be created");
	TEST_ASSERT(error_list->count == 0, "New error list should be empty");

	// Test empty error list printing
	printf("  [DEBUG] Testing empty error list printing:\n");
	cdsl_error_list_print(error_list);

	TEST_ASSERT(!cdsl_error_list_has_errors(error_list),
		    "Empty error list should not have errors");

	cdsl_error_list_free(error_list);
	TEST_ERROR_END();
}

// Test 7: Error list error handling
static void
test_error_list_error_handling()
{
	TEST_ERROR_BEGIN("Error list error handling");

	cdsl_error_list_t* error_list = cdsl_error_list_create();

	// Add NULL error (should be ignored)
	cdsl_error_list_add(error_list, NULL);
	TEST_ASSERT(error_list->count == 0, "Error list should ignore NULL entries");

	// Try to add more errors
	cdsl_error_t* err1 = cdsl_error_create(CDSL_ERR_SYNTAX, 10, 5, "Error 1", "Hint 1");
	cdsl_error_t* err2 = cdsl_error_create(CDSL_ERR_TYPE, 15, 8, "Error 2", "Hint 2");

	cdsl_error_list_add(error_list, err1);
	cdsl_error_list_add(error_list, err2);

	TEST_ASSERT(error_list->count == 2, "Error list should contain 2 entries");

	cdsl_error_list_free(error_list);
	TEST_ERROR_END();
}

// Test 8: Error memory management
static void
test_error_memory_management()
{
	TEST_ERROR_BEGIN("Error memory management");

	// Create many errors to test memory allocation
	cdsl_error_t** errors = malloc(100 * sizeof(cdsl_error_t*));
	TEST_ASSERT_NOT_NULL(errors, "Memory allocation should succeed");

	for (int i = 0; i < 100; i++) {
		char msg[32];
		sprintf(msg, "Memory test error %d", i);

		errors[i] = cdsl_error_create(CDSL_ERR_SYNTAX, i, 1, msg, "Memory test hint");
		TEST_ASSERT_NOT_NULL(errors[i], "Error should be created");
	}

	// Create error list and add all errors
	cdsl_error_list_t* error_list = cdsl_error_list_create();
	for (int i = 0; i < 100; i++) {
		cdsl_error_list_add(error_list, errors[i]);
	}

	TEST_ASSERT(error_list->count == 100, "Error list should contain 100 errors");

	// Free everything
	cdsl_error_list_free(error_list);
	free(errors);

	TEST_ERROR_END();
}

// Test 9: Integration with schema validation error reporting
static void
test_schema_validation_error_reporting()
{
	TEST_ERROR_BEGIN("Schema validation error reporting");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	// Test rule with multiple errors
	const char* dsl =
	    "RULE test { WHEN invalid_var > \"invalid\" THEN unknown_action(\"arg1\", \"arg2\") }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule should parse");

	// Test detailed error collection
	cdsl_error_list_t* errors = cdsl_verify_rule_detailed(rule, schema);
	TEST_ASSERT_NOT_NULL(errors, "Error list should be created");
	TEST_ASSERT(errors->count > 0, "Should collect errors");

	printf("  [DEBUG] Schema validation errors found:\n");
	cdsl_error_list_print(errors);

	// Test that errors contain expected information
	int found_invalid_var = 0;
	int found_invalid_action = 0;
	int found_type_error = 0;

	for (int i = 0; i < errors->count; i++) {
		cdsl_error_t* err = errors->errors[i];
		if (err->message && strstr(err->message, "invalid_var") != NULL) {
			found_invalid_var = 1;
		}
		if (err->message && strstr(err->message, "Unknown action") != NULL) {
			found_invalid_action = 1;
		}
		/* For binary expression, if left operand is VOID, resolve_expr_type returns VOID early.
		 * In this case, we have two semantic errors: unknown var and unknown action.
		 * The type error for the comparison is only reported if operands are not VOID.
		 */
	}

	TEST_ASSERT(found_invalid_var, "Should find invalid variable error");
	TEST_ASSERT(found_invalid_action, "Should find invalid action error");

	cdsl_error_list_free(errors);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);

	TEST_ERROR_END();
}

// Test 10: Error performance test
static void
test_error_performance()
{
	TEST_ERROR_BEGIN("Error performance test");

	// Create many errors
	for (int i = 0; i < 1000; i++) {
		char msg[32];
		sprintf(msg, "Performance error %d", i);

		cdsl_error_t* err =
		    cdsl_error_create(CDSL_ERR_SYNTAX, i, 1, msg, "Performance hint");
		cdsl_error_free(err);
	}

	// Test error list performance
	cdsl_error_list_t* error_list = cdsl_error_list_create();
	for (int i = 0; i < 1000; i++) {
		char msg[32];
		sprintf(msg, "List error %d", i);

		cdsl_error_t* err = cdsl_error_create(CDSL_ERR_SYNTAX, i, 1, msg, "List hint");
		cdsl_error_list_add(error_list, err);
	}

	TEST_ASSERT(error_list->count == 1000, "Error list should contain 1000 errors");

	// Test error list printing performance (limit output)
	printf("  [DEBUG] Testing error list printing (first 5 errors):\n");
	for (int i = 0; i < 5; i++) {
		cdsl_error_print(error_list->errors[i]);
	}

	cdsl_error_list_free(error_list);

	TEST_ERROR_END();
}

int
main()
{
	printf("Running error handling tests...\n");

	// Run all error handling tests
	test_error_creation();
	test_error_list_management();
	test_error_list_capacity_growth();
	test_error_type_validation();
	test_error_null_hint();
	test_empty_error_list();
	test_error_list_error_handling();
	test_error_memory_management();
	test_schema_validation_error_reporting();
	test_error_performance();

	TEST_SUMMARY();
	TEST_EXIT();
}