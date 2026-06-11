/**
 * @file test_json.c
 * @brief JSON utilities tests for C-DSL.
 *
 * Tests JSON parsing, JSON value handling, JSON memory management,
 * and integration with context loading functionality.
 */

#include <cdsl/util/json.h>
#include <cdsl/context.h>
#include <cdsl/schema.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "test.h"

// Test helper macros
#define TEST_JSON_BEGIN(name) TEST_BEGIN(name)
#define TEST_JSON_END() TEST_END()

// Test 1: Basic JSON parsing
static void
test_json_basic_parsing()
{
	TEST_JSON_BEGIN("Basic JSON parsing");

	// Test JSON object
	const char* json_obj = "{\"name\": \"Alice\", \"age\": 25, \"active\": true}";
	cdsl_json_value_t* obj = cdsl_json_parse(json_obj);
	TEST_ASSERT_NOT_NULL(obj, "JSON object should parse");
	TEST_ASSERT(obj->type == CDSL_JSON_OBJECT, "Should be object type");

	// Test JSON array
	const char* json_arr = "[1, 2, 3, 4, 5]";
	cdsl_json_value_t* arr = cdsl_json_parse(json_arr);
	TEST_ASSERT_NOT_NULL(arr, "JSON array should parse");
	TEST_ASSERT(arr->type == CDSL_JSON_ARRAY, "Should be array type");

	// Test JSON string
	const char* json_str = "\"Hello, World!\"";
	cdsl_json_value_t* str = cdsl_json_parse(json_str);
	TEST_ASSERT_NOT_NULL(str, "JSON string should parse");
	TEST_ASSERT(str->type == CDSL_JSON_STRING, "Should be string type");

	// Test JSON number (integer)
	const char* json_num = "42";
	cdsl_json_value_t* num = cdsl_json_parse(json_num);
	TEST_ASSERT_NOT_NULL(num, "JSON number should parse");
	TEST_ASSERT(num->type == CDSL_JSON_NUMBER, "Should be number type");

	// Test JSON boolean (true)
	const char* json_true = "true";
	cdsl_json_value_t* true_val = cdsl_json_parse(json_true);
	TEST_ASSERT_NOT_NULL(true_val, "JSON true should parse");
	TEST_ASSERT(true_val->type == CDSL_JSON_BOOL, "Should be boolean type");
	TEST_ASSERT(true_val->value.bool_val == 1, "Should be true");

	// Test JSON boolean (false)
	const char* json_false = "false";
	cdsl_json_value_t* false_val = cdsl_json_parse(json_false);
	TEST_ASSERT_NOT_NULL(false_val, "JSON false should parse");
	TEST_ASSERT(false_val->type == CDSL_JSON_BOOL, "Should be boolean type");
	TEST_ASSERT(false_val->value.bool_val == 0, "Should be false");

	// Test JSON null
	const char* json_null = "null";
	cdsl_json_value_t* null_val = cdsl_json_parse(json_null);
	TEST_ASSERT_NOT_NULL(null_val, "JSON null should parse");
	TEST_ASSERT(null_val->type == CDSL_JSON_NULL, "Should be null type");

	// Clean up
	cdsl_json_free(obj);
	cdsl_json_free(arr);
	cdsl_json_free(str);
	cdsl_json_free(num);
	cdsl_json_free(true_val);
	cdsl_json_free(false_val);
	cdsl_json_free(null_val);

	TEST_JSON_END();
}

// Test 2: Nested JSON parsing
static void
test_json_nested_parsing()
{
	TEST_JSON_BEGIN("Nested JSON parsing");

	const char* nested_json = "{"
				  "  \"user\": {"
				  "    \"name\": \"Alice\","
				  "    \"age\": 25,"
				  "    \"address\": {"
				  "      \"street\": \"123 Main St\","
				  "      \"city\": \"New York\""
				  "    }"
				  "  },"
				  "  \"scores\": [100, 95, 87],"
				  "  \"active\": true"
				  "}";

	cdsl_json_value_t* root = cdsl_json_parse(nested_json);
	TEST_ASSERT_NOT_NULL(root, "Nested JSON should parse");
	TEST_ASSERT(root->type == CDSL_JSON_OBJECT, "Root should be object");

	// Test nested object access
	cdsl_json_value_t* user = cdsl_json_get_object(root, "user");
	TEST_ASSERT_NOT_NULL(user, "User object should exist");
	TEST_ASSERT(user->type == CDSL_JSON_OBJECT, "User should be object");

	cdsl_json_value_t* name = cdsl_json_get_object(user, "name");
	TEST_ASSERT_NOT_NULL(name, "Name should exist");
	TEST_ASSERT(name->type == CDSL_JSON_STRING, "Name should be string");
	TEST_ASSERT(strcmp(name->value.string_val, "Alice") == 0, "Name should match");

	cdsl_json_value_t* age = cdsl_json_get_object(user, "age");
	TEST_ASSERT_NOT_NULL(age, "Age should exist");
	TEST_ASSERT(age->type == CDSL_JSON_NUMBER, "Age should be number");
	TEST_ASSERT(age->value.number_val == 25, "Age should match");

	// Test nested object
	cdsl_json_value_t* address = cdsl_json_get_object(user, "address");
	TEST_ASSERT_NOT_NULL(address, "Address should exist");
	TEST_ASSERT(address->type == CDSL_JSON_OBJECT, "Address should be object");

	cdsl_json_value_t* city = cdsl_json_get_object(address, "city");
	TEST_ASSERT_NOT_NULL(city, "City should exist");
	TEST_ASSERT(city->type == CDSL_JSON_STRING, "City should be string");
	TEST_ASSERT(strcmp(city->value.string_val, "New York") == 0, "City should match");

	// Test array access
	cdsl_json_value_t* scores = cdsl_json_get_object(root, "scores");
	TEST_ASSERT_NOT_NULL(scores, "Scores should exist");
	TEST_ASSERT(scores->type == CDSL_JSON_ARRAY, "Scores should be array");

	cdsl_json_value_t* score1 = cdsl_json_get_array(scores, 0);
	TEST_ASSERT_NOT_NULL(score1, "First score should exist");
	TEST_ASSERT(score1->type == CDSL_JSON_NUMBER, "Score should be number");
	TEST_ASSERT(score1->value.number_val == 100, "First score should match");

	cdsl_json_value_t* score3 = cdsl_json_get_array(scores, 2);
	TEST_ASSERT_NOT_NULL(score3, "Third score should exist");
	TEST_ASSERT(score3->type == CDSL_JSON_NUMBER, "Score should be number");
	TEST_ASSERT(score3->value.number_val == 87, "Third score should match");

	// Test boolean access
	cdsl_json_value_t* active = cdsl_json_get_object(root, "active");
	TEST_ASSERT_NOT_NULL(active, "Active should exist");
	TEST_ASSERT(active->type == CDSL_JSON_BOOL, "Active should be boolean");
	TEST_ASSERT(active->value.bool_val == 1, "Active should be true");

	cdsl_json_free(root);
	TEST_JSON_END();
}

// Test 3: JSON array handling
static void
test_json_array_handling()
{
	TEST_JSON_BEGIN("JSON array handling");

	// Test array with mixed types
	const char* mixed_array = "[\"string\", 42, true, null, {\"key\": \"value\"}]";
	cdsl_json_value_t* arr = cdsl_json_parse(mixed_array);
	TEST_ASSERT_NOT_NULL(arr, "Mixed array should parse");
	TEST_ASSERT(arr->type == CDSL_JSON_ARRAY, "Should be array type");

	// Test array length
	int length = cdsl_json_array_length(arr);
	TEST_ASSERT(length == 5, "Array should have 5 elements");

	// Test individual elements
	cdsl_json_value_t* elem0 = cdsl_json_get_array(arr, 0);
	TEST_ASSERT_NOT_NULL(elem0, "Element 0 should exist");
	TEST_ASSERT(elem0->type == CDSL_JSON_STRING, "Element 0 should be string");
	TEST_ASSERT(strcmp(elem0->value.string_val, "string") == 0, "Element 0 should match");

	cdsl_json_value_t* elem1 = cdsl_json_get_array(arr, 1);
	TEST_ASSERT_NOT_NULL(elem1, "Element 1 should exist");
	TEST_ASSERT(elem1->type == CDSL_JSON_NUMBER, "Element 1 should be number");
	TEST_ASSERT(elem1->value.number_val == 42, "Element 1 should match");

	cdsl_json_value_t* elem2 = cdsl_json_get_array(arr, 2);
	TEST_ASSERT_NOT_NULL(elem2, "Element 2 should exist");
	TEST_ASSERT(elem2->type == CDSL_JSON_BOOL, "Element 2 should be boolean");
	TEST_ASSERT(elem2->value.bool_val == 1, "Element 2 should be true");

	cdsl_json_value_t* elem3 = cdsl_json_get_array(arr, 3);
	TEST_ASSERT_NOT_NULL(elem3, "Element 3 should exist");
	TEST_ASSERT(elem3->type == CDSL_JSON_NULL, "Element 3 should be null");

	cdsl_json_value_t* elem4 = cdsl_json_get_array(arr, 4);
	TEST_ASSERT_NOT_NULL(elem4, "Element 4 should exist");
	TEST_ASSERT(elem4->type == CDSL_JSON_OBJECT, "Element 4 should be object");

	cdsl_json_value_t* key_val = cdsl_json_get_object(elem4, "key");
	TEST_ASSERT_NOT_NULL(key_val, "Key should exist");
	TEST_ASSERT(key_val->type == CDSL_JSON_STRING, "Key should be string");
	TEST_ASSERT(strcmp(key_val->value.string_val, "value") == 0, "Key should match");

	// Test array bounds
	cdsl_json_value_t* elem_out_of_bounds = cdsl_json_get_array(arr, 10);
	TEST_ASSERT_NULL(elem_out_of_bounds, "Out of bounds access should return NULL");

	cdsl_json_free(arr);
	TEST_JSON_END();
}

// Test 4: JSON error handling
static void
test_json_error_handling()
{
	TEST_JSON_BEGIN("JSON error handling");

	// Test invalid JSON
	const char* invalid_json = "{ invalid json }";
	cdsl_json_value_t* invalid = cdsl_json_parse(invalid_json);
	TEST_ASSERT_NULL(invalid, "Invalid JSON should return NULL");

	// Test empty string
	const char* empty_str = "";
	cdsl_json_value_t* empty = cdsl_json_parse(empty_str);
	TEST_ASSERT_NULL(empty, "Empty string should return NULL");

	// Test NULL input
	cdsl_json_value_t* null_input = cdsl_json_parse(NULL);
	TEST_ASSERT_NULL(null_input, "NULL input should return NULL");

	// Test malformed arrays
	const char* malformed_array = "[1, 2, 3,";
	cdsl_json_value_t* malformed = cdsl_json_parse(malformed_array);
	TEST_ASSERT_NULL(malformed, "Malformed array should return NULL");

	// Test malformed objects
	const char* malformed_obj = "{\"key\": \"value\",";
	cdsl_json_value_t* obj_malformed = cdsl_json_parse(malformed_obj);
	TEST_ASSERT_NULL(obj_malformed, "Malformed object should return NULL");

	TEST_JSON_END();
}

// Test 5: JSON memory management
static void
test_json_memory_management()
{
	TEST_JSON_BEGIN("JSON memory management");

	// Parse multiple JSON objects
	cdsl_json_value_t** json_objects = malloc(10 * sizeof(cdsl_json_value_t*));
	TEST_ASSERT_NOT_NULL(json_objects, "JSON array allocation should succeed");

	for (int i = 0; i < 10; i++) {
		char json_str[64];
		sprintf(json_str, "{\"id\": %d, \"name\": \"item%d\"}", i, i);

		json_objects[i] = cdsl_json_parse(json_str);
		TEST_ASSERT_NOT_NULL(json_objects[i], "JSON should parse");
	}

	// Test nested JSON memory
	const char* nested_json = "{"
				  "  \"level1\": {"
				  "    \"level2\": {"
				  "      \"level3\": \"deep value\""
				  "    }"
				  "  }"
				  "}";

	cdsl_json_value_t* deep_nested = cdsl_json_parse(nested_json);
	TEST_ASSERT_NOT_NULL(deep_nested, "Nested JSON should parse");

	// Access deep nested value
	cdsl_json_value_t* level1 = cdsl_json_get_object(deep_nested, "level1");
	cdsl_json_value_t* level2 = cdsl_json_get_object(level1, "level2");
	cdsl_json_value_t* level3 = cdsl_json_get_object(level2, "level3");

	TEST_ASSERT_NOT_NULL(level3, "Deep value should exist");
	TEST_ASSERT(strcmp(level3->value.string_val, "deep value") == 0, "Deep value should match");

	// Free all JSON objects
	for (int i = 0; i < 10; i++) {
		cdsl_json_free(json_objects[i]);
	}
	free(json_objects);
	cdsl_json_free(deep_nested);

	TEST_JSON_END();
}

// Test 6: Context loading from JSON
static void
test_context_json_loading()
{
	TEST_JSON_BEGIN("Context loading from JSON");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "user.name", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "user.score", CDSL_TYPE_FLOAT);
	cdsl_schema_register_var(schema, "user.is_active", CDSL_TYPE_BOOL);

	// Test JSON with nested objects
	const char* json_context = "{"
				   "  \"user\": {"
				   "    \"age\": 25,"
				   "    \"name\": \"Alice\","
				   "    \"score\": 85.5,"
				   "    \"is_active\": true"
				   "  }"
				   "}";

	cdsl_context_t* ctx = cdsl_context_create(schema);
	TEST_ASSERT_NOT_NULL(ctx, "Context should be created");

	// Load JSON into context
	int result = cdsl_context_load_json(ctx, json_context);
	TEST_ASSERT(result == 1, "JSON loading should succeed");

	// Test variable access
	int age = cdsl_context_get_int(ctx, "user.age", 0);
	TEST_ASSERT(age == 25, "Age should match");

	const char* name = cdsl_context_get_string(ctx, "user.name", NULL);
	TEST_ASSERT_NOT_NULL(name, "Name should exist");
	TEST_ASSERT(strcmp(name, "Alice") == 0, "Name should match");

	double score = cdsl_context_get_float(ctx, "user.score", 0.0);
	TEST_ASSERT(score == 85.5, "Score should match");

	int is_active = cdsl_context_get_bool(ctx, "user.is_active", 0);
	TEST_ASSERT(is_active == 1, "Active should be true");

	// Test non-existent variable
	int non_existent = cdsl_context_get_int(ctx, "nonexistent", 0);
	TEST_ASSERT(non_existent == 0, "Non-existent variable should return 0");

	cdsl_context_free(ctx);
	cdsl_schema_free(schema);

	TEST_JSON_END();
}

// Test 7: Complex context loading
static void
test_complex_context_loading()
{
	TEST_JSON_BEGIN("Complex context loading");

	cdsl_schema_t* schema = cdsl_schema_create();

	// Register many variables
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "user.name", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "user.score", CDSL_TYPE_FLOAT);
	cdsl_schema_register_var(schema, "user.is_active", CDSL_TYPE_BOOL);
	cdsl_schema_register_var(schema, "user.address.street", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "user.address.city", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "user.address.zipcode", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "user.preferences.theme", CDSL_TYPE_STRING);
	cdsl_schema_register_var(schema, "user.preferences.notifications", CDSL_TYPE_BOOL);

	// Complex JSON with nested structures
	const char* complex_json = "{"
				   "  \"user\": {"
				   "    \"age\": 30,"
				   "    \"name\": \"Bob Johnson\","
				   "    \"score\": 92.3,"
				   "    \"is_active\": true,"
				   "    \"address\": {"
				   "      \"street\": \"456 Oak Avenue\","
				   "      \"city\": \"San Francisco\","
				   "      \"zipcode\": 94102"
				   "    },"
				   "    \"preferences\": {"
				   "      \"theme\": \"dark\","
				   "      \"notifications\": false"
				   "    }"
				   "  }"
				   "}";

	cdsl_context_t* ctx = cdsl_context_create(schema);
	TEST_ASSERT_NOT_NULL(ctx, "Context should be created");

	int result = cdsl_context_load_json(ctx, complex_json);
	TEST_ASSERT(result == 1, "Complex JSON loading should succeed");

	// Test all variables
	TEST_ASSERT(cdsl_context_get_int(ctx, "user.age", 0) == 30, "Age should match");
	TEST_ASSERT(strcmp(cdsl_context_get_string(ctx, "user.name", NULL), "Bob Johnson") == 0,
		    "Name should match");
	TEST_ASSERT(cdsl_context_get_float(ctx, "user.score", 0.0) == 92.3, "Score should match");
	TEST_ASSERT(cdsl_context_get_bool(ctx, "user.is_active", 0) == 1, "Active should be true");

	TEST_ASSERT(strcmp(cdsl_context_get_string(ctx, "user.address.street", NULL),
			   "456 Oak Avenue") == 0,
		    "Street should match");
	TEST_ASSERT(
	    strcmp(cdsl_context_get_string(ctx, "user.address.city", NULL), "San Francisco") == 0,
	    "City should match");
	TEST_ASSERT(cdsl_context_get_int(ctx, "user.address.zipcode", 0) == 94102,
		    "Zipcode should match");

	TEST_ASSERT(strcmp(cdsl_context_get_string(ctx, "user.preferences.theme", NULL), "dark") ==
			0,
		    "Theme should match");
	TEST_ASSERT(cdsl_context_get_bool(ctx, "user.preferences.notifications", 0) == 0,
		    "Notifications should be false");

	cdsl_context_free(ctx);
	cdsl_schema_free(schema);

	TEST_JSON_END();
}

// Test 8: JSON context error handling
static void
test_json_context_error_handling()
{
	TEST_JSON_BEGIN("JSON context error handling");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "user.name", CDSL_TYPE_STRING);

	cdsl_context_t* ctx = cdsl_context_create(schema);
	TEST_ASSERT_NOT_NULL(ctx, "Context should be created");

	// Test invalid JSON
	const char* invalid_json = "{ invalid json }";
	int result = cdsl_context_load_json(ctx, invalid_json);
	TEST_ASSERT(result == 0, "Invalid JSON should fail to load");

	// Test JSON with missing required fields
	const char* incomplete_json = "{\"user\": {}}";
	result = cdsl_context_load_json(ctx, incomplete_json);
	TEST_ASSERT(result == 1, "Incomplete JSON should still load (missing fields are optional)");

	// Test JSON with type mismatches
	const char* type_mismatch_json = "{\"user\": {\"age\": \"not_a_number\", \"name\": 123}}";
	result = cdsl_context_load_json(ctx, type_mismatch_json);
	TEST_ASSERT(result == 1, "Type mismatches should be handled gracefully");

	// Test NULL JSON
	result = cdsl_context_load_json(ctx, NULL);
	TEST_ASSERT(result == 0, "NULL JSON should fail to load");

	cdsl_context_free(ctx);
	cdsl_schema_free(schema);

	TEST_JSON_END();
}

// Test 9: JSON performance test
static void
test_json_performance()
{
	TEST_JSON_BEGIN("JSON performance test");

	// Parse many JSON objects
	const char* json_objects[] = {"{\"id\": 1, \"name\": \"item1\"}",
				      "{\"id\": 2, \"name\": \"item2\"}",
				      "{\"id\": 3, \"name\": \"item3\"}",
				      "{\"id\": 4, \"name\": \"item4\"}",
				      "{\"id\": 5, \"name\": \"item5\"}"};

	cdsl_json_value_t** parsed_objects = malloc(5 * sizeof(cdsl_json_value_t*));
	TEST_ASSERT_NOT_NULL(parsed_objects, "JSON array allocation should succeed");

	// Parse objects
	for (int i = 0; i < 5; i++) {
		parsed_objects[i] = cdsl_json_parse(json_objects[i]);
		TEST_ASSERT_NOT_NULL(parsed_objects[i], "JSON should parse");
	}

	// Access values multiple times
	for (int i = 0; i < 100; i++) {
		for (int j = 0; j < 5; j++) {
			cdsl_json_value_t* id = cdsl_json_get_object(parsed_objects[j], "id");
			cdsl_json_value_t* name = cdsl_json_get_object(parsed_objects[j], "name");

			TEST_ASSERT_NOT_NULL(id, "ID should exist");
			TEST_ASSERT_NOT_NULL(name, "Name should exist");
			TEST_ASSERT(id->value.number_val == j + 1, "ID should match");
		}
	}

	// Free all objects
	for (int i = 0; i < 5; i++) {
		cdsl_json_free(parsed_objects[i]);
	}
	free(parsed_objects);

	TEST_JSON_END();
}

// Test 10: JSON edge cases
static void
test_json_edge_cases()
{
	TEST_JSON_BEGIN("JSON edge cases");

	// Test empty object
	const char* empty_obj = "{}";
	cdsl_json_value_t* obj = cdsl_json_parse(empty_obj);
	TEST_ASSERT_NOT_NULL(obj, "Empty object should parse");
	TEST_ASSERT(obj->type == CDSL_JSON_OBJECT, "Should be object type");
	TEST_ASSERT(cdsl_json_object_length(obj) == 0, "Empty object should have 0 keys");

	// Test empty array
	const char* empty_arr = "[]";
	cdsl_json_value_t* arr = cdsl_json_parse(empty_arr);
	TEST_ASSERT_NOT_NULL(arr, "Empty array should parse");
	TEST_ASSERT(arr->type == CDSL_JSON_ARRAY, "Should be array type");
	TEST_ASSERT(cdsl_json_array_length(arr) == 0, "Empty array should have 0 elements");

	// Test deeply nested object
	const char* deep_obj = "{\"a\": {\"b\": {\"c\": {\"d\": {\"e\": \"deep\"}}}}}";
	cdsl_json_value_t* deep = cdsl_json_parse(deep_obj);
	TEST_ASSERT_NOT_NULL(deep, "Deep object should parse");

	cdsl_json_value_t* a = cdsl_json_get_object(deep, "a");
	cdsl_json_value_t* b = cdsl_json_get_object(a, "b");
	cdsl_json_value_t* c = cdsl_json_get_object(b, "c");
	cdsl_json_value_t* d = cdsl_json_get_object(c, "d");
	cdsl_json_value_t* e = cdsl_json_get_object(d, "e");

	TEST_ASSERT_NOT_NULL(e, "Deep value should exist");
	TEST_ASSERT(strcmp(e->value.string_val, "deep") == 0, "Deep value should match");

	// Test JSON with special characters
	const char* special_json = "{\"key\": \"value with \\\"quotes\\\" and \\n newlines\"}";
	cdsl_json_value_t* special = cdsl_json_parse(special_json);
	TEST_ASSERT_NOT_NULL(special, "Special JSON should parse");

	cdsl_json_value_t* key_val = cdsl_json_get_object(special, "key");
	TEST_ASSERT_NOT_NULL(key_val, "Key should exist");
	TEST_ASSERT(strcmp(key_val->value.string_val, "value with \"quotes\" and \n newlines") == 0,
		    "Special chars should match");

	cdsl_json_free(obj);
	cdsl_json_free(arr);
	cdsl_json_free(deep);
	cdsl_json_free(special);

	TEST_JSON_END();
}

int
main()
{
	printf("Running JSON utilities tests...\n");

	// Run all JSON utility tests
	test_json_basic_parsing();
	test_json_nested_parsing();
	test_json_array_handling();
	test_json_error_handling();
	test_json_memory_management();
	test_context_json_loading();
	test_complex_context_loading();
	test_json_context_error_handling();
	test_json_performance();
	test_json_edge_cases();

	TEST_SUMMARY();
	TEST_EXIT();
}
