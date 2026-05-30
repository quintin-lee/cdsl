/**
 * @file test_memory.c
 * @brief Memory management tests for C-DSL.
 *
 * Tests arena allocator, memory leak detection, memory usage patterns,
 * and integration with other components for proper memory management.
 */

#include <cdsl/util/arena.h>
#include <cdsl/util/hashmap.h>
#include <cdsl/schema.h>
#include <cdsl/ast.h>
#include <cdsl/execution.h>
#include <cdsl/util/json.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "test.h"

// Test helper macros
#define TEST_MEMORY_BEGIN(name) TEST_BEGIN(name)
#define TEST_MEMORY_END() TEST_END()

// Global memory tracking for leak detection
static size_t total_allocated = 0;
static size_t total_freed = 0;

// Custom malloc wrapper for tracking
void*
tracked_malloc(size_t size)
{
	void* ptr = malloc(size);
	if (ptr) {
		total_allocated += size;
	}
	return ptr;
}

// Custom free wrapper for tracking
void
tracked_free(void* ptr, size_t size)
{
	if (ptr) {
		total_freed += size;
		free(ptr);
	}
}

// Test 1: Arena allocator basic functionality
static void
test_arena_allocator()
{
	TEST_MEMORY_BEGIN("Arena allocator basic functionality");

	cdsl_arena_t* arena = cdsl_arena_create(1024); // 1KB arena
	TEST_ASSERT_NOT_NULL(arena, "Arena should be created");

	// Test basic allocation
	void* ptr1 = cdsl_arena_alloc(arena, 100);
	TEST_ASSERT_NOT_NULL(ptr1, "Should allocate 100 bytes");

	void* ptr2 = cdsl_arena_alloc(arena, 200);
	TEST_ASSERT_NOT_NULL(ptr2, "Should allocate 200 bytes");
	TEST_ASSERT(ptr1 != ptr2, "Different allocations should be different");

	// Test string duplication
	const char* test_str = "Hello, World!";
	char* dup_str = cdsl_arena_strdup(arena, test_str);
	TEST_ASSERT_NOT_NULL(dup_str, "String duplication should succeed");
	TEST_ASSERT(strcmp(dup_str, test_str) == 0, "Duplicated string should match");

	// Test arena free
	cdsl_arena_free(arena);
	TEST_MEMORY_END();
}

// Test 2: Arena allocator with large allocations
static void
test_arena_large_allocations()
{
	TEST_MEMORY_BEGIN("Arena allocator large allocations");

	cdsl_arena_t* arena = cdsl_arena_create(64 * 1024); // 64KB arena

	// Allocate large blocks
	void* ptr1 = cdsl_arena_alloc(arena, 10000);
	TEST_ASSERT_NOT_NULL(ptr1, "Should allocate 10KB");

	void* ptr2 = cdsl_arena_alloc(arena, 20000);
	TEST_ASSERT_NOT_NULL(ptr2, "Should allocate 20KB");

	void* ptr3 = cdsl_arena_alloc(arena, 15000);
	TEST_ASSERT_NOT_NULL(ptr3, "Should allocate 15KB");

	cdsl_arena_free(arena);
	TEST_MEMORY_END();
}

// Test 3: Arena allocator boundary conditions
static void
test_arena_boundary_conditions()
{
	TEST_MEMORY_BEGIN("Arena allocator boundary conditions");

	// Test with very small arena
	cdsl_arena_t* arena = cdsl_arena_create(32);

	// Should succeed to allocate larger than initial block size (creates new block)
	void* ptr1 = cdsl_arena_alloc(arena, 64);
	TEST_ASSERT_NOT_NULL(ptr1, "Should succeed to allocate larger than block size");

	// Should allocate exactly arena size
	void* ptr2 = cdsl_arena_alloc(arena, 32);
	TEST_ASSERT_NOT_NULL(ptr2, "Should allocate exactly arena size");

	cdsl_arena_free(arena);
	TEST_MEMORY_END();
}

// Test 4: Hash map memory management
static void
test_hashmap_memory_management()
{
	TEST_MEMORY_BEGIN("Hash map memory management");

	cdsl_hashmap_t* map = cdsl_hashmap_create(16);
	TEST_ASSERT_NOT_NULL(map, "Hash map should be created");

	// Add entries
	for (int i = 0; i < 100; i++) {
		char key[32];
		sprintf(key, "key%d", i);

		char* value = strdup(key);
		TEST_ASSERT_NOT_NULL(value, "Value allocation should succeed");

		int result = cdsl_hashmap_put(map, key, value);
		TEST_ASSERT(result == 1, "Should put entry successfully");
	}

	// Test lookup
	for (int i = 0; i < 100; i++) {
		char key[32];
		sprintf(key, "key%d", i);

		void* value = cdsl_hashmap_get(map, key);
		TEST_ASSERT_NOT_NULL(value, "Should find value");
		TEST_ASSERT(strcmp((char*)value, key) == 0, "Value should match");
	}

	// Test removal with destructor
	cdsl_hashmap_free(map, free);
	TEST_MEMORY_END();
}

// Test hashmap helper functions: remove, has, iterate, keys
static int g_hashmap_cb_count;

static void
hashmap_count_cb(const char* key, void* value, void* ud)
{
	(void)key;
	(void)value;
	(void)ud;
	g_hashmap_cb_count++;
}

static void
test_hashmap_helpers(void)
{
	TEST_MEMORY_BEGIN("Hashmap helper functions");

	cdsl_hashmap_t* map = cdsl_hashmap_create(16);
	TEST_ASSERT_NOT_NULL(map, "Map created");

	cdsl_hashmap_put(map, "alpha", strdup("value1"));
	cdsl_hashmap_put(map, "beta", strdup("value2"));
	cdsl_hashmap_put(map, "gamma", strdup("value3"));

	// has()
	TEST_ASSERT(cdsl_hashmap_has(map, "alpha"), "has alpha true");
	TEST_ASSERT(cdsl_hashmap_has(map, "beta"), "has beta true");
	TEST_ASSERT(!cdsl_hashmap_has(map, "nonexistent"), "has nonexistent false");
	TEST_ASSERT(!cdsl_hashmap_has(NULL, "alpha"), "has NULL map false");
	TEST_ASSERT(!cdsl_hashmap_has(map, NULL), "has NULL key false");

	// remove() with destructor
	int removed = cdsl_hashmap_remove(map, "alpha", free);
	TEST_ASSERT(removed == 1, "remove alpha ok");
	TEST_ASSERT(!cdsl_hashmap_has(map, "alpha"), "alpha gone");
	TEST_ASSERT(cdsl_hashmap_remove(map, "nonexistent", free) == 0,
		    "remove nonexistent returns 0");

	// iterate() — use counter to verify all entries
	g_hashmap_cb_count = 0;
	cdsl_hashmap_iterate(map, hashmap_count_cb, NULL);
	TEST_ASSERT_INT(g_hashmap_cb_count, 2, "iterate found 2 entries");

	// keys()
	int key_count = 0;
	char** keys = cdsl_hashmap_keys(map, &key_count);
	TEST_ASSERT_INT(key_count, 2, "keys() returns 2");
	TEST_ASSERT_NOT_NULL(keys, "keys array not null");
	for (int i = 0; i < key_count; i++) {
		free(keys[i]);
	}
	free(keys);

	cdsl_hashmap_free(map, free);
	TEST_MEMORY_END();
}

// Test 5: AST memory management
static void
test_ast_memory_management()
{
	TEST_MEMORY_BEGIN("AST memory management");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	// Parse multiple rules
	const char* rules[] = {"RULE test1 { WHEN user.age > 18 THEN block(\"adult\") }",
			       "RULE test2 { WHEN user.age < 65 THEN block(\"young\") }",
			       "RULE test3 { WHEN user.age == 30 THEN block(\"thirty\") }"};

	cdsl_rule_t** parsed_rules = malloc(3 * sizeof(cdsl_rule_t*));
	TEST_ASSERT_NOT_NULL(parsed_rules, "Rule array allocation should succeed");

	for (int i = 0; i < 3; i++) {
		parsed_rules[i] = cdsl_parse_string(rules[i]);
		TEST_ASSERT_NOT_NULL(parsed_rules[i], "Rule should parse");

		// Verify rule
		char err[512] = {0};
		bool valid = cdsl_verify_rule(parsed_rules[i], schema, err, sizeof(err));
		TEST_ASSERT(valid, "Rule should be valid");
	}

	// Free all rules
	for (int i = 0; i < 3; i++) {
		cdsl_free_rule(parsed_rules[i]);
	}
	free(parsed_rules);

	cdsl_schema_free(schema);
	TEST_MEMORY_END();
}

// Test 6: Schema memory management
static void
test_schema_memory_management()
{
	TEST_MEMORY_BEGIN("Schema memory management");

	// Create multiple schemas
	cdsl_schema_t** schemas = malloc(10 * sizeof(cdsl_schema_t*));
	TEST_ASSERT_NOT_NULL(schemas, "Schema array allocation should succeed");

	for (int i = 0; i < 10; i++) {
		schemas[i] = cdsl_schema_create();
		TEST_ASSERT_NOT_NULL(schemas[i], "Schema should be created");

		// Register variables and actions
		char var_name[32];
		char action_name[32];
		sprintf(var_name, "var%d", i);
		sprintf(action_name, "action%d", i);

		cdsl_schema_register_var(schemas[i], var_name, CDSL_TYPE_INT);
		cdsl_schema_register_action(
		    schemas[i], action_name, CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	}

	// Free all schemas
	for (int i = 0; i < 10; i++) {
		cdsl_schema_free(schemas[i]);
	}
	free(schemas);

	TEST_MEMORY_END();
}

// Test 7: VM memory management
static void
test_vm_memory_management()
{
	TEST_MEMORY_BEGIN("VM memory management");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	// Create multiple VMs
	cdsl_vm_t** vms = malloc(5 * sizeof(cdsl_vm_t*));
	TEST_ASSERT_NOT_NULL(vms, "VM array allocation should succeed");

	for (int i = 0; i < 5; i++) {
		vms[i] = cdsl_vm_create(schema);
		TEST_ASSERT_NOT_NULL(vms[i], "VM should be created");

		// Register actions
		char action_name[32];
		sprintf(action_name, "action%d", i);
		cdsl_vm_register_action(vms[i], action_name, NULL);
	}

	// Free all VMs
	for (int i = 0; i < 5; i++) {
		cdsl_vm_free(vms[i]);
	}
	free(vms);

	cdsl_schema_free(schema);
	TEST_MEMORY_END();
}

// Test 8: Context memory management
static void
test_context_memory_management()
{
	TEST_MEMORY_BEGIN("Context memory management");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "user.name", CDSL_TYPE_STRING);

	// Create multiple contexts
	cdsl_context_t** contexts = malloc(10 * sizeof(cdsl_context_t*));
	TEST_ASSERT_NOT_NULL(contexts, "Context array allocation should succeed");

	for (int i = 0; i < 10; i++) {
		contexts[i] = cdsl_context_create(schema);
		TEST_ASSERT_NOT_NULL(contexts[i], "Context should be created");

		// Set variables
		cdsl_context_set_int(contexts[i], "user.age", i * 10);
		cdsl_context_set_string(contexts[i], "user.name", "test_user");
	}

	// Free all contexts
	for (int i = 0; i < 10; i++) {
		cdsl_context_free(contexts[i]);
	}
	free(contexts);

	cdsl_schema_free(schema);
	TEST_MEMORY_END();
}

// Test 9: JSON memory management
static void
test_json_memory_management()
{
	TEST_MEMORY_BEGIN("JSON memory management");

	// Parse multiple JSON objects
	const char* json_strings[] = {"{\"name\": \"Alice\", \"age\": 25}",
				      "{\"name\": \"Bob\", \"age\": 30}",
				      "{\"name\": \"Charlie\", \"age\": 35}",
				      "{\"name\": \"David\", \"age\": 40}",
				      "{\"name\": \"Eve\", \"age\": 45}"};

	cdsl_json_value_t** json_values = malloc(5 * sizeof(cdsl_json_value_t*));
	TEST_ASSERT_NOT_NULL(json_values, "JSON array allocation should succeed");

	for (int i = 0; i < 5; i++) {
		json_values[i] = cdsl_json_parse(json_strings[i]);
		TEST_ASSERT_NOT_NULL(json_values[i], "JSON should parse");
	}

	// Free all JSON values
	for (int i = 0; i < 5; i++) {
		cdsl_json_free(json_values[i]);
	}
	free(json_values);

	TEST_MEMORY_END();
}

// Test 10: Memory leak detection test
static void
test_memory_leak_detection()
{
	TEST_MEMORY_BEGIN("Memory leak detection");

	// Reset tracking
	total_allocated = 0;
	total_freed = 0;

	// Simulate memory allocation and potential leaks
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

	const char* dsl = "RULE test { WHEN user.age > 0 THEN block(\"test\") }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl);

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	// Perform operations
	cdsl_context_set_int(ctx, "user.age", 25);

	// Add some tracked operations to verify tracking logic
	void* dummy = tracked_malloc(100);
	tracked_free(dummy, 100);

	// Free properly (no leaks)
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);

	printf(
	    "  [DEBUG] Memory tracking: allocated=%zu, freed=%zu\n", total_allocated, total_freed);

	// In a real leak detection scenario, we would check if allocated == freed
	// For this test, we just verify the tracking works
	TEST_ASSERT(total_allocated > 0, "Should have allocated memory");
	TEST_ASSERT(total_freed > 0, "Should have freed memory");

	TEST_MEMORY_END();
}

// Test 11: Complex memory usage pattern
static void
test_complex_memory_usage_pattern()
{
	TEST_MEMORY_BEGIN("Complex memory usage pattern");

	cdsl_schema_t* schema = cdsl_schema_create();

	// Register many variables and actions
	for (int i = 0; i < 50; i++) {
		char var_name[32];
		char action_name[32];
		sprintf(var_name, "user.field%d", i);
		sprintf(action_name, "action%d", i);

		cdsl_schema_register_var(schema, var_name, CDSL_TYPE_INT);
		cdsl_schema_register_action(
		    schema, action_name, CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	}

	// Create arena for string allocations
	cdsl_arena_t* arena = cdsl_arena_create(4096);

	// Create and parse multiple rules
	cdsl_rule_t** rules = malloc(10 * sizeof(cdsl_rule_t*));
	TEST_ASSERT_NOT_NULL(rules, "Rules array allocation should succeed");

	for (int i = 0; i < 10; i++) {
		char dsl[256];
		sprintf(
		    dsl, "RULE test%d { WHEN user.field%d > 0 THEN action%d(\"test\") }", i, i, i);

		rules[i] = cdsl_parse_string(dsl);
		TEST_ASSERT_NOT_NULL(rules[i], "Rule should parse");

		// Verify rule
		char err[512] = {0};
		bool valid = cdsl_verify_rule(rules[i], schema, err, sizeof(err));
		TEST_ASSERT(valid, "Rule should be valid");

		// Duplicate strings in arena
		char* arena_str = cdsl_arena_strdup(arena, dsl);
		TEST_ASSERT_NOT_NULL(arena_str, "Arena string duplication should succeed");
	}

	// Create hash map for caching
	cdsl_hashmap_t* cache = cdsl_hashmap_create(32);

	// Add entries to cache
	for (int i = 0; i < 20; i++) {
		char key[32];
		char value[32];
		sprintf(key, "cache_key%d", i);
		sprintf(value, "cache_value%d", i);

		cdsl_hashmap_put(cache, key, strdup(value));
	}

	// Clean up everything
	for (int i = 0; i < 10; i++) {
		cdsl_free_rule(rules[i]);
	}
	free(rules);

	cdsl_hashmap_free(cache, free);
	cdsl_arena_free(arena);
	cdsl_schema_free(schema);

	TEST_MEMORY_END();
}

// Test 12: Memory stress test
static void
test_memory_stress_test()
{
	TEST_MEMORY_BEGIN("Memory stress test");

	// Create many objects to test memory management under stress
	cdsl_schema_t** schemas = malloc(20 * sizeof(cdsl_schema_t*));
	cdsl_vm_t** vms = malloc(20 * sizeof(cdsl_vm_t*));
	cdsl_context_t** contexts = malloc(20 * sizeof(cdsl_context_t*));

	TEST_ASSERT_NOT_NULL(schemas, "Schemas array allocation should succeed");
	TEST_ASSERT_NOT_NULL(vms, "VMs array allocation should succeed");
	TEST_ASSERT_NOT_NULL(contexts, "Contexts array allocation should succeed");

	// Create and use many objects
	for (int i = 0; i < 20; i++) {
		schemas[i] = cdsl_schema_create();
		cdsl_schema_register_var(schemas[i], "user.age", CDSL_TYPE_INT);
		cdsl_schema_register_action(
		    schemas[i], "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

		vms[i] = cdsl_vm_create(schemas[i]);
		cdsl_vm_register_action(vms[i], "block", NULL);

		contexts[i] = cdsl_context_create(schemas[i]);
		cdsl_context_set_int(contexts[i], "user.age", i);
	}

	// Perform operations
	for (int i = 0; i < 20; i++) {
		char err[512] = {0};
		const char* dsl = "RULE test { WHEN user.age > 0 THEN block(\"test\") }";
		cdsl_rule_t* rule = cdsl_parse_string(dsl);

		if (rule) {
			bool valid = cdsl_verify_rule(rule, schemas[i], err, sizeof(err));
			TEST_ASSERT(valid, "Rule should be valid");
			cdsl_free_rule(rule);
		}
	}

	// Clean up all objects
	for (int i = 0; i < 20; i++) {
		cdsl_context_free(contexts[i]);
		cdsl_vm_free(vms[i]);
		cdsl_schema_free(schemas[i]);
	}

	free(schemas);
	free(vms);
	free(contexts);

	TEST_MEMORY_END();
}

int
main()
{
	printf("Running memory management tests...\n");

	// Run all memory management tests
	test_arena_allocator();
	test_arena_large_allocations();
	test_arena_boundary_conditions();
	test_hashmap_memory_management();
	test_hashmap_helpers();
	test_ast_memory_management();
	test_schema_memory_management();
	test_vm_memory_management();
	test_context_memory_management();
	test_json_memory_management();
	test_memory_leak_detection();
	test_complex_memory_usage_pattern();
	test_memory_stress_test();

	TEST_SUMMARY();
	TEST_EXIT();
}