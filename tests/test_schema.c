/**
 * @file test_schema.c
 * @brief Comprehensive schema validation tests for C-DSL.
 *
 * Tests schema creation, variable registration, action registration,
 * rule validation, error handling, and type checking.
 */

#include <cdsl/schema.h>
#include <cdsl/ast.h>
#include <cdsl/util/error.h>
#include <cdsl/execution.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "test.h"

void test_analyze_rule(void);

// Test helper macros
#define TEST_SCHEMA_BEGIN(name) TEST_BEGIN(name)
#define TEST_SCHEMA_END() TEST_END()

// Test 1: Basic schema creation and variable registration
static void
test_schema_creation()
{
	TEST_SCHEMA_BEGIN("Basic schema creation");

	cdsl_schema_t* schema = cdsl_schema_create();
	TEST_ASSERT_NOT_NULL(schema, "Schema should be created");

	// Register variables with different types
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "user.name", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "user.is_active", CDSL_TYPE_BOOL);
	cdsl_schema_register_var(schema, "user.score", CDSL_TYPE_FLOAT);

	// Verify variable registration
	TEST_ASSERT(schema->vars != NULL, "Variables should be registered");

	cdsl_schema_free(schema);
	TEST_SCHEMA_END();
}

// Test 2: Action registration with different signatures
static void
test_action_registration()
{
	TEST_SCHEMA_BEGIN("Action registration");

	cdsl_schema_t* schema = cdsl_schema_create();

	// Register various action signatures
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	cdsl_schema_register_action(schema, "score", CDSL_TYPE_VOID, 1, CDSL_TYPE_INT);
	cdsl_schema_register_action(
	    schema, "log", CDSL_TYPE_VOID, 2, CDSL_TYPE_STRING, CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "validate", CDSL_TYPE_BOOL, 0);
	cdsl_schema_register_action(schema, "compute", CDSL_TYPE_FLOAT, 1, CDSL_TYPE_INT);

	// Verify action registration
	TEST_ASSERT(schema->actions != NULL, "Actions list should exist");

	// Verify action registration
	TEST_ASSERT(schema->actions != NULL, "Actions should be registered");

	cdsl_schema_free(schema);
	TEST_SCHEMA_END();
}

// Test 3: Rule validation - valid simple rule
static void
test_rule_validation_simple_valid()
{
	TEST_SCHEMA_BEGIN("Simple rule validation (valid)");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	const char* dsl = "RULE check_age { META { description = \"Age check\" } WHEN user.age >= "
			  "18 THEN block(\"adult\") }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule should parse successfully");

	// Test fast verification
	char err[512] = {0};
	bool valid = cdsl_verify_rule(rule, schema, err, sizeof(err));
	TEST_ASSERT(valid, "Valid rule should pass verification");
	TEST_ASSERT(strlen(err) == 0, "No error message for valid rule");

	// Test detailed verification
	cdsl_error_list_t* errors = cdsl_verify_rule_detailed(rule, schema);
	TEST_ASSERT(errors != NULL, "Detailed verification should return error list");
	TEST_ASSERT(errors->count == 0, "No errors for valid rule");

	cdsl_error_list_free(errors);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_SCHEMA_END();
}

// Test 4: Rule validation - invalid variable
static void
test_rule_validation_invalid_var()
{
	TEST_SCHEMA_BEGIN("Rule validation (invalid variable)");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	const char* dsl = "RULE check { WHEN invalid_var > 10 THEN block(\"test\") }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule should parse successfully");

	// Test fast verification - should fail
	char err[512] = {0};
	bool valid = cdsl_verify_rule(rule, schema, err, sizeof(err));
	TEST_ASSERT(!valid, "Invalid rule should fail verification");
	TEST_ASSERT(strlen(err) > 0, "Should have error message");
	TEST_ASSERT(strstr(err, "invalid_var") != NULL, "Error should mention invalid variable");

	// Test detailed verification
	cdsl_error_list_t* errors = cdsl_verify_rule_detailed(rule, schema);
	TEST_ASSERT(errors != NULL, "Detailed verification should return error list");
	TEST_ASSERT(errors->count > 0, "Should have errors for invalid rule");

	cdsl_error_list_free(errors);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_SCHEMA_END();
}

// Test 5: Rule validation - invalid action signature
static void
test_rule_validation_invalid_action()
{
	TEST_SCHEMA_BEGIN("Rule validation (invalid action)");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	// Action with wrong argument count
	const char* dsl = "RULE check { WHEN user.age > 18 THEN block(\"arg1\", \"arg2\") }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule should parse successfully");

	char err[512] = {0};
	bool valid = cdsl_verify_rule(rule, schema, err, sizeof(err));
	TEST_ASSERT(!valid, "Invalid action should fail verification");
	TEST_ASSERT(strlen(err) > 0, "Should have error message");

	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_SCHEMA_END();
}

// Test 6: Type compatibility checking
static void
test_type_compatibility()
{
	TEST_SCHEMA_BEGIN("Type compatibility checking");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "user.score", CDSL_TYPE_FLOAT);
	cdsl_schema_register_var(schema, "user.name", CDSL_TYPE_STRING);
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	// Test valid type promotion (INT + FLOAT)
	const char* dsl1 = "RULE test { WHEN user.age + user.score > 100 THEN block(\"ok\") }";
	cdsl_rule_t* rule1 = cdsl_parse_string(dsl1, NULL);
	TEST_ASSERT_NOT_NULL(rule1, "Rule with type promotion should parse");

	char err[512] = {0};
	bool valid1 = cdsl_verify_rule(rule1, schema, err, sizeof(err));
	TEST_ASSERT(valid1, "Type promotion should be allowed");

	// Test invalid type comparison (INT vs STRING)
	const char* dsl2 = "RULE test { WHEN user.age > \"invalid\" THEN block(\"ok\") }";
	cdsl_rule_t* rule2 = cdsl_parse_string(dsl2, NULL);
	TEST_ASSERT_NOT_NULL(rule2, "Rule with type mismatch should parse");

	bool valid2 = cdsl_verify_rule(rule2, schema, err, sizeof(err));
	TEST_ASSERT(!valid2, "Type mismatch should be rejected");

	cdsl_free_rule(rule1);
	cdsl_free_rule(rule2);
	cdsl_schema_free(schema);
	TEST_SCHEMA_END();
}

// Test 7: String comparison validation
static void
test_string_comparison_validation()
{
	TEST_SCHEMA_BEGIN("String comparison validation");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.name", CDSL_TYPE_STRING);
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	// Test valid string comparisons
	const char* dsl1 = "RULE test1 { WHEN user.name == \"Alice\" THEN block(\"match\") }";
	const char* dsl2 = "RULE test2 { WHEN user.name != \"Bob\" THEN block(\"not_match\") }";

	cdsl_rule_t* rule1 = cdsl_parse_string(dsl1, NULL);
	cdsl_rule_t* rule2 = cdsl_parse_string(dsl2, NULL);

	char err[512] = {0};
	bool valid1 = cdsl_verify_rule(rule1, schema, err, sizeof(err));
	bool valid2 = cdsl_verify_rule(rule2, schema, err, sizeof(err));

	TEST_ASSERT(valid1, "String == should be allowed");
	TEST_ASSERT(valid2, "String != should be allowed");

	// Test invalid string comparison (only == and != allowed)
	const char* dsl3 = "RULE test3 { WHEN user.name > \"Alice\" THEN block(\"invalid\") }";
	cdsl_rule_t* rule3 = cdsl_parse_string(dsl3, NULL);

	bool valid3 = cdsl_verify_rule(rule3, schema, err, sizeof(err));
	TEST_ASSERT(!valid3, "String > should be rejected");

	cdsl_free_rule(rule1);
	cdsl_free_rule(rule2);
	cdsl_free_rule(rule3);
	cdsl_schema_free(schema);
	TEST_SCHEMA_END();
}

// Test 8: Schema edge cases
static void
test_schema_edge_cases()
{
	TEST_SCHEMA_BEGIN("Schema edge cases");

	// Test NULL schema handling
	cdsl_rule_t* rule =
	    cdsl_parse_string("RULE test { WHEN x > 0 THEN block(\"test\") }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule should parse");

	char err[512] = {0};
	bool valid = cdsl_verify_rule(rule, NULL, err, sizeof(err));
	TEST_ASSERT(!valid, "Verification should fail with NULL schema");

	// Test empty schema
	cdsl_schema_t* empty_schema = cdsl_schema_create();
	valid = cdsl_verify_rule(rule, empty_schema, err, sizeof(err));
	TEST_ASSERT(!valid, "Verification should fail with empty schema");

	// Test duplicate variable registration
	cdsl_schema_register_var(empty_schema, "x", CDSL_TYPE_INT);
	cdsl_schema_register_var(empty_schema, "x", CDSL_TYPE_INT); // Should not crash

	cdsl_schema_free(empty_schema);
	cdsl_free_rule(rule);
	TEST_SCHEMA_END();
}

// Test 9: Complex rule validation
static void
test_complex_rule_validation()
{
	TEST_SCHEMA_BEGIN("Complex rule validation");

	cdsl_schema_t* schema = cdsl_schema_create();

	// Register variables for complex rule
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "user.score", CDSL_TYPE_FLOAT);
	cdsl_schema_register_var(schema, "user.name", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "user.is_premium", CDSL_TYPE_BOOL);

	// Register actions
	cdsl_schema_register_action(schema, "approve", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	cdsl_schema_register_action(schema, "reject", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	cdsl_schema_register_action(schema, "score", CDSL_TYPE_VOID, 1, CDSL_TYPE_INT);

	// Complex rule with multiple conditions
	const char* dsl = "RULE complex_audit {"
			  "  META { description = \"Complex user audit\" pass_threshold = \"80\" }"
			  "  METRIC age_check {"
			  "    META { weight = \"30\" }"
			  "    CASE user.age >= 25 THEN score(30)"
			  "    CASE user.age >= 18 THEN score(15)"
			  "    DEFAULT score(0)"
			  "  }"
			  "  METRIC score_check {"
			  "    META { weight = \"40\" is_critical = \"true\" }"
			  "    CASE user.score >= 80 THEN score(40)"
			  "    CASE user.score >= 60 THEN score(20)"
			  "    DEFAULT score(0)"
			  "  }"
			  "  METRIC name_check {"
			  "    META { weight = \"30\" }"
			  "    CASE user.name != \"\" THEN score(30)"
			  "    DEFAULT score(0)"
			  "  }"
			  "}";

	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Complex rule should parse");

	char err[512] = {0};
	bool valid = cdsl_verify_rule(rule, schema, err, sizeof(err));
	TEST_ASSERT(valid, "Complex rule should pass verification");
	TEST_ASSERT(strlen(err) == 0, "No error message for valid complex rule");

	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_SCHEMA_END();
}

// Test 10: Schema performance test
static void
test_schema_performance()
{
	TEST_SCHEMA_BEGIN("Schema performance test");

	cdsl_schema_t* schema = cdsl_schema_create();

	// Register many variables and actions
	for (int i = 0; i < 100; i++) {
		char var_name[32];
		char action_name[32];
		sprintf(var_name, "user.field%d", i);
		sprintf(action_name, "action%d", i);

		cdsl_schema_register_var(schema, var_name, CDSL_TYPE_INT);
		cdsl_schema_register_action(
		    schema, action_name, CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	}

	// Test verification performance
	const char* dsl = "RULE perf_test { WHEN user.field0 > 0 THEN action0(\"test\") }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Performance test rule should parse");

	// Run multiple verification tests
	for (int i = 0; i < 1000; i++) {
		char err[512] = {0};
		bool valid = cdsl_verify_rule(rule, schema, err, sizeof(err));
		TEST_ASSERT(valid, "Performance test should pass");
	}

	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_SCHEMA_END();
}

int
main()
{
	printf("Running schema validation tests...\n");

	// Run all schema tests
	test_schema_creation();
	test_action_registration();
	test_rule_validation_simple_valid();
	test_rule_validation_invalid_var();
	test_rule_validation_invalid_action();
	test_type_compatibility();
	test_string_comparison_validation();
	test_schema_edge_cases();
	test_complex_rule_validation();
	test_schema_performance();

	test_analyze_rule();
	TEST_SUMMARY();
	TEST_EXIT();
}

void
test_analyze_rule(void)
{
	TEST_BEGIN("static analysis warnings");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "x", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "y", CDSL_TYPE_FLOAT);

	/* Always-true WHEN */
	cdsl_rule_t* r1 = cdsl_parse_string("RULE r { WHEN 1 == 1 THEN block(\"ok\") }", NULL);
	cdsl_error_list_t* w1 = cdsl_analyze_rule(r1, schema);
	TEST_ASSERT_NOT_NULL(w1, "always-true WHEN produces warning");
	cdsl_error_list_free(w1);
	cdsl_free_rule(r1);

	/* Always-false WHEN */
	cdsl_rule_t* r2 = cdsl_parse_string("RULE r { WHEN 2 == 3 THEN block(\"ok\") }", NULL);
	cdsl_error_list_t* w2 = cdsl_analyze_rule(r2, schema);
	TEST_ASSERT_NOT_NULL(w2, "always-false WHEN produces warning");
	cdsl_error_list_free(w2);
	cdsl_free_rule(r2);

	/* Good rule — no warnings */
	cdsl_rule_t* r3 = cdsl_parse_string("RULE r { WHEN x > 5 THEN block(\"ok\") }", NULL);
	cdsl_error_list_t* w3 = cdsl_analyze_rule(r3, schema);
	TEST_ASSERT_NULL(w3, "valid rule produces no warnings");
	cdsl_free_rule(r3);

	/* Dead metric CASE — always-false condition */
	cdsl_rule_t* r4 = cdsl_parse_string(
	    "RULE r { META { w = \"100\" } "
	    "METRIC m { CASE 5 == 6 THEN score(0) CASE x > 10 THEN score(50) DEFAULT score(0) } }",
	    NULL);
	cdsl_error_list_t* w4 = cdsl_analyze_rule(r4, schema);
	TEST_ASSERT_NOT_NULL(w4, "always-false CASE produces warning");
	TEST_ASSERT_INT(w4->count, 1, "one warning for dead CASE");
	cdsl_error_list_free(w4);
	cdsl_free_rule(r4);

	/* Always-true CASE shadows subsequent */
	cdsl_rule_t* r5 = cdsl_parse_string(
	    "RULE r { META { w = \"100\" } "
	    "METRIC m { CASE 5 == 5 THEN score(0) CASE x > 10 THEN score(50) DEFAULT score(0) } }",
	    NULL);
	cdsl_error_list_t* w5 = cdsl_analyze_rule(r5, schema);
	TEST_ASSERT_NOT_NULL(w5, "always-true CASE warns about shadowing");
	cdsl_error_list_free(w5);
	cdsl_free_rule(r5);

	cdsl_schema_free(schema);
	TEST_END();
}