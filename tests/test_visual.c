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
static void test_rule_visualization() {
    TEST_VISUAL_BEGIN("Basic rule visualization");
    
    cdsl_schema_t* schema = cdsl_schema_create();
    cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
    cdsl_schema_register_var(schema, "user.name", CDSL_TYPE_STRING);
    cdsl_schema_register_action(, "", , 0);
    
    cdsl_ruleset_t* ruleset = cdsl_ruleset_create();
    
    // Create a simple rule
    const char* rule_dsl = "if user.age > 18 then log \"Adult\"";
    cdsl_rule_t* rule = cdsl_parse_string(rule_dsl);
    TEST_ASSERT_NOT_NULL(rule, "Rule should parse");
    
    // Add rule to ruleset
    cdsl_ruleset_add(ruleset, rule, 1);
    
    // Test visualization
    char* dot_output = cdsl_ruleset_to_dot(ruleset);
    TEST_ASSERT_NOT_NULL(dot_output, "Visualization should generate output");
    TEST_ASSERT(strlen(dot_output) > 0, "Visualization should not be empty");
    
    // Check for expected content in visualization
    TEST_ASSERT(strstr(dot_output, "digraph") != NULL, "Should contain digraph declaration");
    TEST_ASSERT(strstr(dot_output, "user.age") != NULL, "Should contain user.age reference");
    TEST_ASSERT(strstr(dot_output, "log") != NULL, "Should contain log action");
    
    free(dot_output);
    cdsl_free_rule(rule);
    cdsl_ruleset_free(ruleset);
    cdsl_schema_free(schema);
    
    TEST_VISUAL_END();
}

// Test 2: Complex ruleset visualization
static void test_complex_ruleset_visualization() {
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
        "if user.age > 18 then log \"Adult\"",
        "if user.score > 90 then email \"High score!\"",
        "if user.age < 18 and user.score > 80 then alert \"Young high performer\"",
        "if user.name == \"Admin\" then log \"Administrator detected\""
    };
    
    cdsl_rule_t* rules_array[4];
    for (int i = 0; i < 4; i++) {
        rules_array[i] = cdsl_parse_string(rules[i]);
        TEST_ASSERT_NOT_NULL(rules_array[i], "Rule should parse");
        cdsl_ruleset_add(ruleset, rules_array[i], 1);
    }
    
    // Test visualization
    char* dot_output = cdsl_ruleset_to_dot(ruleset);
    TEST_ASSERT_NOT_NULL(dot_output, "Visualization should generate output");
    TEST_ASSERT(strlen(dot_output) > 0, "Visualization should not be empty");
    
    // Check for all expected content
    TEST_ASSERT(strstr(dot_output, "digraph") != NULL, "Should contain digraph declaration");
    TEST_ASSERT(strstr(dot_output, "user.age") != NULL, "Should contain user.age reference");
    TEST_ASSERT(strstr(dot_output, "user.score") != NULL, "Should contain user.score reference");
    TEST_ASSERT(strstr(dot_output, "user.name") != NULL, "Should contain user.name reference");
    TEST_ASSERT(strstr(dot_output, "log") != NULL, "Should contain log action");
    TEST_ASSERT(strstr(dot_output, "email") != NULL, "Should contain email action");
    TEST_ASSERT(strstr(dot_output, "alert") != NULL, "Should contain alert action");
    
    free(dot_output);
    for (int i = 0; i < 4; i++) {
        cdsl_free_rule(rules_array[i]);
    }
    cdsl_ruleset_free(ruleset);
    cdsl_schema_free(schema);
    
    TEST_VISUAL_END();
}

// Test 3: Empty ruleset visualization
static void test_empty_ruleset_visualization() {
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
static void test_visualization_error_handling() {
    TEST_VISUAL_BEGIN("Visualization error handling");
    
    // Test with NULL ruleset
    char* result = cdsl_ruleset_to_dot(NULL);
    TEST_ASSERT_NULL(result, "Visualization with NULL ruleset should return NULL");
    
    TEST_VISUAL_END();
}

// Test 5: Performance test for visualization
static void test_visualization_performance() {
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
        char rule_dsl[64];
        sprintf(rule_dsl, "if var%d > %d then action1", (i % 3) + 1, i);
        
        cdsl_rule_t* rule = cdsl_parse_string(rule_dsl);
        TEST_ASSERT_NOT_NULL(rule, "Rule should parse");
        cdsl_ruleset_add(ruleset, rule, 1);
    }
    
    // Test visualization performance
    char* dot_output = cdsl_ruleset_to_dot(ruleset);
    TEST_ASSERT_NOT_NULL(dot_output, "Visualization should generate output");
    TEST_ASSERT(strlen(dot_output) > 0, "Visualization should not be empty");
    
    // Verify all variables and actions are present
    TEST_ASSERT(strstr(dot_output, "var1") != NULL, "Should contain var1");
    TEST_ASSERT(strstr(dot_output, "var2") != NULL, "Should contain var2");
    TEST_ASSERT(strstr(dot_output, "var3") != NULL, "Should contain var3");
    TEST_ASSERT(strstr(dot_output, "action1") != NULL, "Should contain action1");
    TEST_ASSERT(strstr(dot_output, "action2") != NULL, "Should contain action2");
    
    free(dot_output);
    cdsl_ruleset_free(ruleset);
    cdsl_schema_free(schema);
    
    TEST_VISUAL_END();
}

int main() {
    printf("Running visualization tests...\n");
    
    // Run all visualization tests
    test_rule_visualization();
    test_complex_ruleset_visualization();
    test_empty_ruleset_visualization();
    test_visualization_error_handling();
    test_visualization_performance();
    
    TEST_SUMMARY();
    TEST_EXIT();
}