/**
 * @file test_codegen.c
 * @brief Code generation tests for C-DSL.
 *
 * Tests DSL to C code generation functionality including simple rules,
 * complex expressions, templates, and error handling.
 */

#include <cdsl/cdsl.h>
#include <cdsl/context.h>
#include <cdsl/ast.h>
#include <cdsl/schema.h>
#include <cdsl/ruleset.h>
#include <cdsl/execution.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "test.h"

// Test helper macros
#define TEST_CODEGEN_BEGIN(name) TEST_BEGIN(name)
#define TEST_CODEGEN_END() TEST_END()

// Test 1: Simple rule code generation
static void
test_simple_rule_codegen()
{
	TEST_CODEGEN_BEGIN("Simple rule code generation");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	const char* dsl = "RULE check_age { META { description = \"Age check\" } WHEN user.age >= "
			  "18 THEN block(\"adult\") }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule should parse");

	// Generate C code
	char* c_code = cdsl_codegen_rule_to_c(rule, schema);
	TEST_ASSERT_NOT_NULL(c_code, "C code should be generated");
	TEST_ASSERT(strlen(c_code) > 0, "Generated code should not be empty");

	// Check for expected patterns in generated code
	TEST_ASSERT(strstr(c_code, "int cdsl_eval_rule_") != NULL,
		    "Generated code should contain function name");
	TEST_ASSERT(strstr(c_code, "user.age") != NULL,
		    "Generated code should contain variable references");
	TEST_ASSERT(strstr(c_code, "block") != NULL, "Generated code should contain action calls");
	TEST_ASSERT(strstr(c_code, "if") != NULL, "Generated code should contain control flow");

	free(c_code);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_CODEGEN_END();
}

// Test 2: Metric rule code generation
static void
test_metric_rule_codegen()
{
	TEST_CODEGEN_BEGIN("Metric rule code generation");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "score", CDSL_TYPE_VOID, 1, CDSL_TYPE_INT);
	cdsl_schema_register_action(
	    schema, "fail_metric", CDSL_TYPE_VOID, 2, CDSL_TYPE_INT, CDSL_TYPE_STRING);

	const char* dsl =
	    "RULE scoring {"
	    "  META { description = \"Age scoring\" pass_threshold = \"80\" "
	    "partial_threshold = \"50\" }"
	    "  METRIC age_check {"
	    "    META { description = \"Age evaluation\" weight = \"100\" is_critical = \"true\" }"
	    "    CASE user.age >= 18 THEN score(100)"
	    "    DEFAULT fail_metric(0, \"too_young\")"
	    "  }"
	    "}";

	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Metric rule should parse");

	char* c_code = cdsl_codegen_rule_to_c(rule, schema);
	TEST_ASSERT_NOT_NULL(c_code, "C code should be generated");
	TEST_ASSERT(strlen(c_code) > 0, "Generated code should not be empty");

	// Check for metric-specific patterns
	TEST_ASSERT(strstr(c_code, "goto") != NULL, "Metric rules should use goto dispatch");
	TEST_ASSERT(strstr(c_code, "score") != NULL,
		    "Generated code should contain score function calls");
	TEST_ASSERT(strstr(c_code, "fail_metric") != NULL,
		    "Generated code should contain fail_metric function calls");
	TEST_ASSERT(strstr(c_code, "is_critical") != NULL,
		    "Generated code should handle critical metrics");

	free(c_code);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_CODEGEN_END();
}

// Test 3: Template inheritance code generation
static void
test_template_inheritance_codegen()
{
	TEST_CODEGEN_BEGIN("Template inheritance code generation");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "user.score", CDSL_TYPE_FLOAT);
	cdsl_schema_register_action(schema, "score", CDSL_TYPE_VOID, 1, CDSL_TYPE_INT);
	cdsl_schema_register_action(
	    schema, "fail_metric", CDSL_TYPE_VOID, 2, CDSL_TYPE_INT, CDSL_TYPE_STRING);

	// Register template
	const char* template_dsl = "TEMPLATE base_template {"
				   "  METRIC age_check {"
				   "    META { description = \"Age check\" weight = \"50\" }"
				   "    CASE user.age >= 18 THEN score(50)"
				   "    DEFAULT score(0)"
				   "  }"
				   "}";

	cdsl_rule_t* template_rule = cdsl_parse_string(template_dsl, NULL);
	TEST_ASSERT_NOT_NULL(template_rule, "Template should parse");
	cdsl_template_register(template_rule);

	// Create rule that extends template
	const char* dsl = "RULE extended_rule EXTENDS base_template {"
			  "  META { description = \"Extended rule\" }"
			  "  METRIC score_check {"
			  "    META { description = \"Score check\" weight = \"50\" }"
			  "    CASE user.score >= 80 THEN score(50)"
			  "    DEFAULT score(0)"
			  "  }"
			  "}";

	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Extended rule should parse");

	char* c_code = cdsl_codegen_rule_to_c(rule, schema);
	TEST_ASSERT_NOT_NULL(c_code, "C code should be generated");
	TEST_ASSERT(strlen(c_code) > 0, "Generated code should not be empty");

	// Check that template metrics are included
	TEST_ASSERT(strstr(c_code, "age_check") != NULL,
		    "Generated code should include template metric");
	TEST_ASSERT(strstr(c_code, "score_check") != NULL,
		    "Generated code should include custom metric");

	free(c_code);
	cdsl_free_rule(rule);
	cdsl_free_rule(template_rule);
	cdsl_schema_free(schema);
	TEST_CODEGEN_END();
}

// Test 4: Code generation to file
static void
test_codegen_to_file()
{
	TEST_CODEGEN_BEGIN("Code generation to file");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	const char* dsl = "RULE check_age { WHEN user.age >= 18 THEN block(\"adult\") }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule should parse");

	// Generate code to file
	const char* test_file = "/tmp/test_generated_code.c";
	int result = cdsl_codegen_to_file(rule, schema, test_file);

	TEST_ASSERT(result == 1, "Code generation to file should succeed");

	// Read and verify the generated file
	FILE* fp = fopen(test_file, "r");
	TEST_ASSERT(fp != NULL, "Generated file should exist");

	fseek(fp, 0, SEEK_END);
	long file_size = ftell(fp);
	fseek(fp, 0, SEEK_SET);

	TEST_ASSERT(file_size > 0, "Generated file should not be empty");

	char* file_content = malloc(file_size + 1);
	fread(file_content, 1, file_size, fp);
	file_content[file_size] = '\0';

	fclose(fp);

	// Verify content
	TEST_ASSERT(strstr(file_content, "int cdsl_eval_rule_") != NULL,
		    "Generated file should contain function name");
	TEST_ASSERT(strstr(file_content, "user.age") != NULL,
		    "Generated file should contain variable references");

	free(file_content);
	remove(test_file); // Clean up
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_CODEGEN_END();
}

// Test 5: Complex rule code generation
static void
test_complex_rule_codegen()
{
	TEST_CODEGEN_BEGIN("Complex rule code generation");

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

	// Complex rule
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

	char* c_code = cdsl_codegen_rule_to_c(rule, schema);
	TEST_ASSERT_NOT_NULL(c_code, "C code should be generated");
	TEST_ASSERT(strlen(c_code) > 1000, "Complex rule should generate substantial code");

	// Check for complex patterns
	TEST_ASSERT(strstr(c_code, "is_critical") != NULL,
		    "Generated code should handle critical metrics");
	TEST_ASSERT(strstr(c_code, "pass_threshold") != NULL,
		    "Generated code should handle thresholds");
	TEST_ASSERT(strstr(c_code, "total_score") != NULL, "Generated code should handle scoring");
	TEST_ASSERT(strstr(c_code, "switch") != NULL || strstr(c_code, "if") != NULL,
		    "Generated code should have control flow");

	free(c_code);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_CODEGEN_END();
}

// Test 6: Error handling in code generation
static void
test_codegen_error_handling()
{
	TEST_CODEGEN_BEGIN("Code generation error handling");

	// Test with NULL rule
	cdsl_schema_t* schema = cdsl_schema_create();
	char* c_code = cdsl_codegen_rule_to_c(NULL, schema);
	TEST_ASSERT_NULL(c_code, "Code generation should fail with NULL rule");

	// Test with NULL schema
	const char* dsl = "RULE test { WHEN x > 0 THEN block(\"test\") }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule should parse");

	c_code = cdsl_codegen_rule_to_c(rule, NULL);
	TEST_ASSERT_NULL(c_code, "Code generation should fail with NULL schema");

	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_CODEGEN_END();
}

// Test 7: Code generation with multiple actions
static void
test_codegen_multiple_actions()
{
	TEST_CODEGEN_BEGIN("Code generation with multiple actions");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "user.score", CDSL_TYPE_FLOAT);

	// Register multiple actions
	cdsl_schema_register_action(schema, "approve", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	cdsl_schema_register_action(schema, "reject", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	cdsl_schema_register_action(schema, "score", CDSL_TYPE_VOID, 1, CDSL_TYPE_INT);
	cdsl_schema_register_action(
	    schema, "log", CDSL_TYPE_VOID, 2, CDSL_TYPE_STRING, CDSL_TYPE_INT);

	const char* dsl = "RULE multiple_actions {"
			  "  META { description = \"Multiple actions test\" }"
			  "  METRIC check1 {"
			  "    META { weight = \"50\" }"
			  "    CASE user.age >= 18 THEN score(50)"
			  "    DEFAULT reject(\"too_young\")"
			  "  }"
			  "  METRIC check2 {"
			  "    META { weight = \"50\" }"
			  "    CASE user.score >= 80 THEN approve(\"excellent\")"
			  "    CASE user.score >= 60 THEN log(\"medium\", 1)"
			  "    DEFAULT log(\"low\", 0)"
			  "  }"
			  "}";

	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule should parse");

	char* c_code = cdsl_codegen_rule_to_c(rule, schema);
	TEST_ASSERT_NOT_NULL(c_code, "C code should be generated");

	// Check that all actions are referenced
	TEST_ASSERT(strstr(c_code, "approve") != NULL,
		    "Generated code should contain approve action");
	TEST_ASSERT(strstr(c_code, "reject") != NULL,
		    "Generated code should contain reject action");
	TEST_ASSERT(strstr(c_code, "score") != NULL, "Generated code should contain score action");
	TEST_ASSERT(strstr(c_code, "log") != NULL, "Generated code should contain log action");

	free(c_code);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_CODEGEN_END();
}

// Test 8: Code generation with string literals
static void
test_codegen_string_literals()
{
	TEST_CODEGEN_BEGIN("Code generation with string literals");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.name", CDSL_TYPE_STRING);
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	const char* dsl = "RULE test { WHEN user.name == \"Alice\" THEN block(\"Hello Alice!\") }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule should parse");

	char* c_code = cdsl_codegen_rule_to_c(rule, schema);
	TEST_ASSERT_NOT_NULL(c_code, "C code should be generated");

	// Check that string literals are properly handled
	TEST_ASSERT(strstr(c_code, "\"Alice\"") != NULL,
		    "Generated code should contain string literal");
	TEST_ASSERT(strstr(c_code, "\"Hello Alice!\"") != NULL,
		    "Generated code should contain string literal");

	free(c_code);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_CODEGEN_END();
}

// Test 9: Performance test for code generation
static void
test_codegen_performance()
{
	TEST_CODEGEN_BEGIN("Code generation performance test");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "score", CDSL_TYPE_VOID, 1, CDSL_TYPE_INT);

	const char* dsl = "RULE perf_test { WHEN user.age > 0 THEN score(10) }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule should parse");

	// Generate code multiple times
	for (int i = 0; i < 100; i++) {
		char* c_code = cdsl_codegen_rule_to_c(rule, schema);
		TEST_ASSERT_NOT_NULL(c_code, "C code should be generated");
		free(c_code);
	}

	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_CODEGEN_END();
}

// Test 10: Code generation with complex expressions
static void
test_codegen_complex_expressions()
{
	TEST_CODEGEN_BEGIN("Code generation with complex expressions");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "user.score", CDSL_TYPE_FLOAT);
	cdsl_schema_register_var(schema, "user.is_active", CDSL_TYPE_BOOL);
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	const char* dsl = "RULE complex_expr {"
			  "  WHEN ((user.age >= 18 && user.age <= 65) || user.is_active == true) "
			  "&& !user.is_active"
			  "  THEN block(\"complex_condition\")"
			  "}";

	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule should parse");

	char* c_code = cdsl_codegen_rule_to_c(rule, schema);
	TEST_ASSERT_NOT_NULL(c_code, "C code should be generated");

	// Check for complex expression handling
	TEST_ASSERT(strstr(c_code, "&&") != NULL || strstr(c_code, "and") != NULL,
		    "Generated code should handle AND");
	TEST_ASSERT(strstr(c_code, "||") != NULL || strstr(c_code, "or") != NULL,
		    "Generated code should handle OR");
	TEST_ASSERT(strstr(c_code, "!") != NULL || strstr(c_code, "not") != NULL,
		    "Generated code should handle NOT");

	free(c_code);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_CODEGEN_END();
}

// Test 11: Ruleset code generation
static void
test_ruleset_codegen()
{
	TEST_CODEGEN_BEGIN("Ruleset code generation");

	cdsl_ruleset_t* ruleset = cdsl_ruleset_create();
	cdsl_schema_t* schema = cdsl_schema_create();

	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "user.score", CDSL_TYPE_FLOAT);

	const char* dsl1 = "RULE rule1 { WHEN user.age > 18 THEN score(10) }";
	const char* dsl2 = "RULE rule2 { WHEN user.score > 50 THEN score(20) }";

	cdsl_ruleset_add(ruleset, cdsl_parse_string(dsl1, NULL), 1);
	cdsl_ruleset_add(ruleset, cdsl_parse_string(dsl2, NULL), 2);

	// Generate H and C content
	char* h_code = cdsl_codegen_ruleset_to_h(ruleset, schema, "my_rules");
	char* c_code = cdsl_codegen_ruleset_to_c(ruleset, schema, "my_rules");

	TEST_ASSERT_NOT_NULL(h_code, "Header code should be generated");
	TEST_ASSERT_NOT_NULL(c_code, "Implementation code should be generated");

	// Check header content
	TEST_ASSERT(strstr(h_code, "int cdsl_eval_ruleset_my_rules") != NULL,
		    "Header should contain ruleset function prototype");
	TEST_ASSERT(strstr(h_code, "int cdsl_eval_rule_rule1") != NULL,
		    "Header should contain rule1 prototype");

	// Check implementation content
	TEST_ASSERT(strstr(c_code, "int cdsl_eval_ruleset_my_rules") != NULL,
		    "Implementation should contain ruleset function");
	TEST_ASSERT(strstr(c_code, "cdsl_eval_rule_rule1(get_int, NULL, ctx, ud)") != NULL,
		    "Implementation should call rule1");

	free(h_code);
	free(c_code);
	cdsl_ruleset_free(ruleset);
	cdsl_schema_free(schema);
	TEST_CODEGEN_END();
}

int
main()
{
	printf("Running code generation tests...\n");

	// Run all code generation tests
	test_simple_rule_codegen();
	test_metric_rule_codegen();
	test_template_inheritance_codegen();
	test_codegen_to_file();
	test_complex_rule_codegen();
	test_codegen_error_handling();
	test_codegen_multiple_actions();
	test_codegen_string_literals();
	test_codegen_performance();
	test_codegen_complex_expressions();
	test_ruleset_codegen();

	TEST_SUMMARY();
	TEST_EXIT();
}
