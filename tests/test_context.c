/**
 * @file test_context.c
 * @brief Execution context tests for C-DSL.
 *
 * Tests context variable creation, get/set for all types,
 * NULL-safe operations, and context lifecycle management.
 */

#include <cdsl/context.h>
#include <cdsl/schema.h>
#include <cdsl/util/error.h>
#include <cdsl/util/portability.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "test.h"

#define TEST_CONTEXT_BEGIN(name) TEST_BEGIN(name)
#define TEST_CONTEXT_END() TEST_END()

/* Test 1: Context create and free */
static void
test_context_create_free(void)
{
	TEST_CONTEXT_BEGIN("Context create and free");

	cdsl_schema_t* schema = cdsl_schema_create();
	TEST_ASSERT_NOT_NULL(schema, "Schema should be created");

	cdsl_context_t* ctx = cdsl_context_create(schema);
	TEST_ASSERT_NOT_NULL(ctx, "Context should be created");

	cdsl_context_free(ctx);
	cdsl_schema_free(schema);

	/* NULL-safe free */
	cdsl_context_free(NULL);
	TEST_ASSERT(1, "cdsl_context_free(NULL) should not crash");

	TEST_CONTEXT_END();
}

/* Test 2: Set and get int variables */
static void
test_context_set_get_int(void)
{
	TEST_CONTEXT_BEGIN("Context set/get int");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_context_set_int(ctx, "age", 42);
	TEST_ASSERT_INT(cdsl_context_get_int(ctx, "age", -1), 42, "Get int should return 42");
	TEST_ASSERT_INT(
	    cdsl_context_get_int(ctx, "missing", -1), -1, "Get missing int should return default");

	cdsl_context_free(ctx);
	cdsl_schema_free(schema);
	TEST_CONTEXT_END();
}

/* Test 3: Set and get float variables */
static void
test_context_set_get_float(void)
{
	TEST_CONTEXT_BEGIN("Context set/get float");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_context_set_float(ctx, "score", 3.14);
	TEST_ASSERT_INT((int)(cdsl_context_get_float(ctx, "score", 0) * 100),
			314,
			"Get float should return 3.14");
	TEST_ASSERT_INT((int)(cdsl_context_get_float(ctx, "missing", 0) * 100),
			0,
			"Get missing float should return default");

	cdsl_context_free(ctx);
	cdsl_schema_free(schema);
	TEST_CONTEXT_END();
}

/* Test 4: Set and get bool variables */
static void
test_context_set_get_bool(void)
{
	TEST_CONTEXT_BEGIN("Context set/get bool");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_context_set_bool(ctx, "active", 1);
	TEST_ASSERT_INT(cdsl_context_get_bool(ctx, "active", 0), 1, "Get bool should return true");
	TEST_ASSERT_INT(
	    cdsl_context_get_bool(ctx, "missing", 0), 0, "Get missing bool should return default");

	cdsl_context_set_bool(ctx, "inactive", 0);
	TEST_ASSERT_INT(
	    cdsl_context_get_bool(ctx, "inactive", 1), 0, "Get bool should return false");

	cdsl_context_free(ctx);
	cdsl_schema_free(schema);
	TEST_CONTEXT_END();
}

/* Test 5: Set and get string variables */
static void
test_context_set_get_string(void)
{
	TEST_CONTEXT_BEGIN("Context set/get string");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_context_set_string(ctx, "name", "Alice");
	TEST_ASSERT_STR(
	    cdsl_context_get_string(ctx, "name", ""), "Alice", "Get string should return Alice");
	TEST_ASSERT_STR(cdsl_context_get_string(ctx, "missing", "default"),
			"default",
			"Get missing string should return default");

	cdsl_context_free(ctx);
	cdsl_schema_free(schema);
	TEST_CONTEXT_END();
}

/* Test 6: Set and get date variables */
static void
test_context_set_get_date(void)
{
	TEST_CONTEXT_BEGIN("Context set/get date");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_context_t* ctx = cdsl_context_create(schema);

	time_t now = (time_t)1234567890;
	cdsl_context_set_date(ctx, "birthdate", now);
	TEST_ASSERT_INT((int)cdsl_context_get_date(ctx, "birthdate", 0),
			1234567890,
			"Get date should return 1234567890");
	TEST_ASSERT_INT((int)cdsl_context_get_date(ctx, "missing", 0),
			0,
			"Get missing date should return default");

	cdsl_context_free(ctx);
	cdsl_schema_free(schema);
	TEST_CONTEXT_END();
}

/* Test 7: Set and get long variables */
static void
test_context_set_get_long(void)
{
	TEST_CONTEXT_BEGIN("Context set/get long");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_context_t* ctx = cdsl_context_create(schema);

	int64_t big = 9223372036854775807LL;
	cdsl_context_set_long(ctx, "big", big);
	TEST_ASSERT(cdsl_context_get_long(ctx, "big", 0) == big,
		    "Get long should return big value");
	TEST_ASSERT_INT(
	    cdsl_context_get_long(ctx, "missing", 0), 0, "Get missing long should return default");

	cdsl_context_free(ctx);
	cdsl_schema_free(schema);
	TEST_CONTEXT_END();
}

/* Test 8: NULL-safe context operations */
static void
test_context_null_safe(void)
{
	TEST_CONTEXT_BEGIN("Context NULL-safe operations");

	/* All setters should be NULL-safe */
	cdsl_context_set_int(NULL, "x", 1);
	cdsl_context_set_float(NULL, "x", 1.0);
	cdsl_context_set_bool(NULL, "x", 1);
	cdsl_context_set_string(NULL, "x", "y");
	cdsl_context_set_date(NULL, "x", 1);
	cdsl_context_set_long(NULL, "x", 1);

	/* All getters should be NULL-safe */
	TEST_ASSERT_INT(
	    cdsl_context_get_int(NULL, "x", 0), 0, "Get int NULL should return default");
	TEST_ASSERT_INT(
	    (int)cdsl_context_get_float(NULL, "x", 0), 0, "Get float NULL should return default");
	TEST_ASSERT_INT(
	    cdsl_context_get_bool(NULL, "x", 0), 0, "Get bool NULL should return default");
	TEST_ASSERT_STR(
	    cdsl_context_get_string(NULL, "x", "d"), "d", "Get string NULL should return default");
	TEST_ASSERT_INT(
	    (int)cdsl_context_get_date(NULL, "x", 0), 0, "Get date NULL should return default");
	TEST_ASSERT(cdsl_context_get_long(NULL, "x", 0) == 0,
		    "Get long NULL should return default");

	TEST_CONTEXT_END();
}

/* Test 9: Context with schema-bound variables */
static void
test_context_schema_bound(void)
{
	TEST_CONTEXT_BEGIN("Context schema-bound variables");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "user.name", CDSL_TYPE_STRING);

	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_context_set_int(ctx, "user.age", 30);
	cdsl_context_set_string(ctx, "user.name", "Bob");

	TEST_ASSERT_INT(
	    cdsl_context_get_int(ctx, "user.age", -1), 30, "Schema-bound int should work");
	TEST_ASSERT_STR(cdsl_context_get_string(ctx, "user.name", ""),
			"Bob",
			"Schema-bound string should work");

	cdsl_context_free(ctx);
	cdsl_schema_free(schema);
	TEST_CONTEXT_END();
}

static void
test_context_remove_array(void)
{
	TEST_CONTEXT_BEGIN("Context remove array frees elements cleanly");
	cdsl_context_t* ctx = cdsl_context_create(NULL);
	TEST_ASSERT_INT(
	    cdsl_context_load_json(ctx, "{\"items\": [\"elem1\", \"elem2\"]}"), 1, "load json");
	TEST_ASSERT_INT(cdsl_context_remove(ctx, "items"), 1, "remove array returns 1");
	TEST_ASSERT_INT(cdsl_context_remove(ctx, "items"), 0, "second remove returns 0");
	cdsl_context_free(ctx);
	TEST_CONTEXT_END();
}

static void
test_context_set_string_oom_rollback(void)
{
	TEST_CONTEXT_BEGIN("Context set string OOM transactional rollback");

	cdsl_schema_t* s = cdsl_schema_create();
	cdsl_schema_register_var(s, "user.name", CDSL_TYPE_STRING);
	cdsl_context_t* ctx = cdsl_context_create(s);

	/* Set initial value */
	cdsl_context_set_string(ctx, "user.name", "Initial");
	TEST_ASSERT_STR(
	    cdsl_context_get_string(ctx, "user.name", ""), "Initial", "Initial value matches");

	/* Simulate OOM when modifying existing value: old value must be preserved */
	cdsl_test_set_oom_countdown(0); /* force next allocation to fail */
	cdsl_context_set_string(ctx, "user.name", "Modified");
	TEST_ASSERT_STR(
	    cdsl_context_get_string(ctx, "user.name", ""), "Initial", "Old value preserved on OOM");

	/* Reset hook and add new key under OOM */
	cdsl_test_set_oom_countdown(-1);
	cdsl_test_set_oom_countdown(0);
	cdsl_context_set_string(ctx, "user.new_key", "NewVal");
	TEST_ASSERT_STR(cdsl_context_get_string(ctx, "user.new_key", "missing"),
			"missing",
			"No corrupted node inserted");

	cdsl_test_set_oom_countdown(-1);
	cdsl_context_free(ctx);
	cdsl_schema_free(s);
	TEST_CONTEXT_END();
}

int
main(void)
{
	printf("Running context tests...\n");

	test_context_create_free();
	test_context_set_get_int();
	test_context_set_get_float();
	test_context_set_get_bool();
	test_context_set_get_string();
	test_context_set_get_date();
	test_context_set_get_long();
	test_context_null_safe();
	test_context_schema_bound();
	test_context_remove_array();
	test_context_set_string_oom_rollback();

	TEST_SUMMARY();
	TEST_EXIT();
}
