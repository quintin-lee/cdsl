/**
 * @file test_visual.c
 * @brief Visualization tests for C-DSL.
 *
 * Tests Graphviz visualization functionality including rule visualization,
 * dependency graph generation, and visual output formatting.
 */

#include <cdsl/visual.h>
#include <cdsl/ruleset.h>
#include <cdsl/schema.h>
#include <cdsl/ast.h>
#include <cdsl/execution.h>
#include <cdsl/util/error.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "test.h"

// Test helper macros
#define TEST_VISUAL_BEGIN(name) TEST_BEGIN(name)
#define TEST_VISUAL_END() TEST_END()

// Test 1: Basic rule visualization
static void
test_rule_visualization()
{
	TEST_VISUAL_BEGIN("Basic rule visualization");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "user.name", CDSL_TYPE_STRING);
	cdsl_schema_register_action(schema, "log", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	cdsl_ruleset_t* ruleset = cdsl_ruleset_create();

	// Create a simple rule
	const char* rule_dsl = "RULE adult_check { WHEN user.age > 18 THEN log(\"Adult\") }";
	cdsl_rule_t* rule = cdsl_parse_string(rule_dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule should parse");

	// Add rule to ruleset
	cdsl_ruleset_add(ruleset, rule, 1);

	// Test visualization
	char* dot_output = cdsl_ruleset_to_dot(ruleset);
	TEST_ASSERT_NOT_NULL(dot_output, "Visualization should generate output");
	TEST_ASSERT(strlen(dot_output) > 0, "Visualization should not be empty");

	char* rule_dot = cdsl_rule_to_dot(rule);
	TEST_ASSERT_NOT_NULL(rule_dot, "Rule visualization should generate output");

	// Check for expected content in visualization
	TEST_ASSERT(strstr(dot_output, "digraph") != NULL, "Should contain digraph declaration");
	TEST_ASSERT(strstr(rule_dot, "user.age") != NULL, "Should contain user.age reference");
	TEST_ASSERT(strstr(rule_dot, "log") != NULL, "Should contain log action");

	free(dot_output);
	free(rule_dot);
	cdsl_ruleset_free(ruleset);
	cdsl_schema_free(schema);

	TEST_VISUAL_END();
}

// Test 2: Complex ruleset visualization
static void
test_complex_ruleset_visualization()
{
	TEST_VISUAL_BEGIN("Complex ruleset visualization");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "user.score", CDSL_TYPE_FLOAT);
	cdsl_schema_register_var(schema, "user.name", CDSL_TYPE_STRING);
	cdsl_schema_register_action(schema, "log", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	cdsl_schema_register_action(schema, "email", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	cdsl_schema_register_action(schema, "alert", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	cdsl_ruleset_t* ruleset = cdsl_ruleset_create();

	// Create multiple rules
	const char* rules[] = {
	    "RULE rule1 { WHEN user.age > 18 THEN log(\"Adult\") }",
	    "RULE rule2 { WHEN user.score > 90 THEN email(\"High score!\") }",
	    ("RULE rule3 { WHEN user.age < 18 AND user.score > 80 THEN alert(\"Young high "
	     "performer\") }"),
	    "RULE rule4 { WHEN user.name == \"Admin\" THEN log(\"Administrator detected\") }"};

	cdsl_rule_t* rules_array[4];
	for (int i = 0; i < 4; i++) {
		rules_array[i] = cdsl_parse_string(rules[i], NULL);
		TEST_ASSERT_NOT_NULL(rules_array[i], "Rule should parse");
		cdsl_ruleset_add(ruleset, rules_array[i], 1);
	}

	// Test visualization
	char* dot_output = cdsl_ruleset_to_dot(ruleset);
	TEST_ASSERT_NOT_NULL(dot_output, "Visualization should generate output");
	TEST_ASSERT(strlen(dot_output) > 0, "Visualization should not be empty");

	// Check for all expected content
	TEST_ASSERT(strstr(dot_output, "digraph") != NULL, "Should contain digraph declaration");
	TEST_ASSERT(strstr(dot_output, "rule1") != NULL, "Should contain rule1 reference");
	TEST_ASSERT(strstr(dot_output, "rule2") != NULL, "Should contain rule2 reference");
	TEST_ASSERT(strstr(dot_output, "rule3") != NULL, "Should contain rule3 reference");
	TEST_ASSERT(strstr(dot_output, "rule4") != NULL, "Should contain rule4 reference");

	free(dot_output);
	cdsl_ruleset_free(ruleset);
	cdsl_schema_free(schema);

	TEST_VISUAL_END();
}

// Test 3: Empty ruleset visualization
static void
test_empty_ruleset_visualization()
{
	TEST_VISUAL_BEGIN("Empty ruleset visualization");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_ruleset_t* ruleset = cdsl_ruleset_create();

	// Test visualization of empty ruleset
	char* dot_output = cdsl_ruleset_to_dot(ruleset);
	TEST_ASSERT_NOT_NULL(dot_output, "Visualization should generate output");
	TEST_ASSERT(strlen(dot_output) > 0, "Visualization should not be empty");

	// Should still contain digraph declaration but no rules
	TEST_ASSERT(strstr(dot_output, "digraph") != NULL, "Should contain digraph declaration");

	free(dot_output);
	cdsl_ruleset_free(ruleset);
	cdsl_schema_free(schema);

	TEST_VISUAL_END();
}

// Test 4: Visualization error handling
static void
test_visualization_error_handling()
{
	TEST_VISUAL_BEGIN("Visualization error handling");

	// Test with NULL ruleset
	char* result = cdsl_ruleset_to_dot(NULL);
	TEST_ASSERT_NULL(result, "Visualization with NULL ruleset should return NULL");

	TEST_VISUAL_END();
}

// Test 5: Performance test for visualization
static void
test_visualization_performance()
{
	TEST_VISUAL_BEGIN("Visualization performance test");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "var1", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "var2", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "var3", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "action1", CDSL_TYPE_VOID, 0);
	cdsl_schema_register_action(schema, "action2", CDSL_TYPE_VOID, 0);

	cdsl_ruleset_t* ruleset = cdsl_ruleset_create();

	// Create many rules
	for (int i = 0; i < 20; i++) {
		char rule_dsl[128];
		sprintf(
		    rule_dsl, "RULE rule%d { WHEN var%d > %d THEN action1() }", i, (i % 3) + 1, i);

		cdsl_rule_t* rule = cdsl_parse_string(rule_dsl, NULL);
		TEST_ASSERT_NOT_NULL(rule, "Rule should parse");
		cdsl_ruleset_add(ruleset, rule, 1);
	}

	// Test visualization performance
	char* dot_output = cdsl_ruleset_to_dot(ruleset);
	TEST_ASSERT_NOT_NULL(dot_output, "Visualization should generate output");
	TEST_ASSERT(strlen(dot_output) > 0, "Visualization should not be empty");

	// Verify all variables and actions are present
	TEST_ASSERT(strstr(dot_output, "rule0") != NULL, "Should contain rule0");
	TEST_ASSERT(strstr(dot_output, "rule10") != NULL, "Should contain rule10");
	TEST_ASSERT(strstr(dot_output, "rule19") != NULL, "Should contain rule19");

	free(dot_output);
	cdsl_ruleset_free(ruleset);
	cdsl_schema_free(schema);

	TEST_VISUAL_END();
}

// Test 6: Visualize rule with array expression
static void
test_visualize_array_expression(void)
{
	TEST_VISUAL_BEGIN("Visualize rule with array expression");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "items", CDSL_TYPE_ARRAY);

	cdsl_rule_t* rule =
	    cdsl_parse_string("RULE check_items { WHEN count(items) == 3 THEN log(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule should parse");

	char* dot = cdsl_rule_to_dot(rule);
	TEST_ASSERT_NOT_NULL(dot, "Dot output should not be NULL");
	TEST_ASSERT(strstr(dot, "digraph") != NULL, "Should contain digraph");

	free(dot);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_VISUAL_END();
}

// Test 7: Visualize rule with unary expression
static void
test_visualize_unary_expression(void)
{
	TEST_VISUAL_BEGIN("Visualize rule with unary expression");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "active", CDSL_TYPE_BOOL);

	cdsl_rule_t* rule =
	    cdsl_parse_string("RULE check_active { WHEN !active THEN log(\"inactive\") }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule should parse");

	char* dot = cdsl_rule_to_dot(rule);
	TEST_ASSERT_NOT_NULL(dot, "Dot output should not be NULL");
	TEST_ASSERT(strstr(dot, "NOT") != NULL, "Should contain NOT operator");

	free(dot);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_VISUAL_END();
}

// Test 8: Visualize rule with function call expression
static void
test_visualize_call_expression(void)
{
	TEST_VISUAL_BEGIN("Visualize rule with function call expression");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "name", CDSL_TYPE_STRING);

	cdsl_rule_t* rule =
	    cdsl_parse_string("RULE check_name { WHEN strlen(name) > 5 THEN log(\"long\") }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule should parse");

	char* dot = cdsl_rule_to_dot(rule);
	TEST_ASSERT_NOT_NULL(dot, "Dot output should not be NULL");
	TEST_ASSERT(strstr(dot, "strlen") != NULL, "Should contain strlen call");

	free(dot);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_VISUAL_END();
}

// Test 9: Visualize metric rule
static void
test_visualize_metric_rule(void)
{
	TEST_VISUAL_BEGIN("Visualize metric rule");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "score", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "alert", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	cdsl_rule_t* rule =
	    cdsl_parse_string("RULE score_check { METRIC m1 { META { weight = \"100\" } CASE score "
			      "> 90 THEN alert(\"high\") DEFAULT alert(\"normal\") } }",
			      NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule should parse");

	char* dot = cdsl_rule_to_dot(rule);
	TEST_ASSERT_NOT_NULL(dot, "Dot output should not be NULL");
	TEST_ASSERT(strstr(dot, "metric") != NULL, "Should contain metric node");
	TEST_ASSERT(strstr(dot, "CASE") != NULL, "Should contain CASE node");
	TEST_ASSERT(strstr(dot, "DEFAULT") != NULL, "Should contain DEFAULT node");

	free(dot);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_VISUAL_END();
}

// Test 10: Rule to dot file
static void
test_visualize_rule_to_dot_file(void)
{
	TEST_VISUAL_BEGIN("Visualize rule to dot file");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "x", CDSL_TYPE_INT);

	cdsl_rule_t* rule =
	    cdsl_parse_string("RULE file_test { WHEN x > 0 THEN log(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule should parse");

#ifdef _WIN32
	const char* filepath = "test_rule.dot";
#else
	const char* filepath = "/tmp/test_rule.dot";
#endif
	int result = cdsl_rule_to_dot_file(rule, filepath);
	TEST_ASSERT_INT(result, 1, "Rule to dot file should return 1");

	// Verify file was created
	FILE* f = fopen(filepath, "r");
	TEST_ASSERT_NOT_NULL(f, "Dot file should exist");
	if (f) {
		char buf[256];
		size_t n = fread(buf, 1, sizeof(buf) - 1, f);
		buf[n] = '\0';
		fclose(f);
		TEST_ASSERT(strstr(buf, "digraph") != NULL, "File should contain digraph");
		remove(filepath);
	}

	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_VISUAL_END();
}

// Test 11: Ruleset to dot file
static void
test_visualize_ruleset_to_dot_file(void)
{
	TEST_VISUAL_BEGIN("Visualize ruleset to dot file");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "x", CDSL_TYPE_INT);

	cdsl_rule_t* rule =
	    cdsl_parse_string("RULE file_test { WHEN x > 0 THEN log(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule should parse");

	cdsl_ruleset_t* ruleset = cdsl_ruleset_create();
	cdsl_ruleset_add(ruleset, rule, 1);

#ifdef _WIN32
	const char* filepath = "test_ruleset.dot";
#else
	const char* filepath = "/tmp/test_ruleset.dot";
#endif
	int result = cdsl_ruleset_to_dot_file(ruleset, filepath);
	TEST_ASSERT_INT(result, 1, "Ruleset to dot file should return 1");

	// Verify file was created
	FILE* f = fopen(filepath, "r");
	TEST_ASSERT_NOT_NULL(f, "Dot file should exist");
	if (f) {
		char buf[256];
		size_t n = fread(buf, 1, sizeof(buf) - 1, f);
		buf[n] = '\0';
		fclose(f);
		TEST_ASSERT(strstr(buf, "digraph") != NULL, "File should contain digraph");
		remove(filepath);
	}

	cdsl_ruleset_free(ruleset);
	cdsl_schema_free(schema);
	TEST_VISUAL_END();
}

// Test 13: NULL rule visualization
static void
test_visualize_null_rule(void)
{
	TEST_VISUAL_BEGIN("Visualize NULL rule");

	char* dot = cdsl_rule_to_dot(NULL);
	TEST_ASSERT_NULL(dot, "NULL rule should return NULL");

	TEST_VISUAL_END();
}

// Test 14: Arithmetic and unary negation visualization
static void
test_visualize_arithmetic_and_neg(void)
{
	TEST_VISUAL_BEGIN("Visualize arithmetic operators and unary NEG");

	const char* dsl = "RULE arith_vis { WHEN (a + b - 1) * 2 / 3 > -5 THEN log(\"ok\") }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Arithmetic rule should parse");

	char* dot = cdsl_rule_to_dot(rule);
	TEST_ASSERT_NOT_NULL(dot, "DOT output should be generated without crashing");
	TEST_ASSERT(strstr(dot, "NEG") != NULL, "DOT output should label unary minus as NEG");
	TEST_ASSERT(strstr(dot, "+") != NULL, "DOT output should contain +");
	TEST_ASSERT(strstr(dot, "*") != NULL, "DOT output should contain *");

	free(dot);
	cdsl_free_rule(rule);
	TEST_VISUAL_END();
}

int
main()
{
	printf("Running visualization tests...\n");

	// Run all visualization tests
	test_rule_visualization();
	test_complex_ruleset_visualization();
	test_empty_ruleset_visualization();
	test_visualization_error_handling();
	test_visualization_performance();
	test_visualize_array_expression();
	test_visualize_unary_expression();
	test_visualize_call_expression();
	test_visualize_metric_rule();

	test_visualize_rule_to_dot_file();
	test_visualize_ruleset_to_dot_file();
	test_visualize_null_rule();
	test_visualize_arithmetic_and_neg();

	TEST_SUMMARY();
	TEST_EXIT();
}
