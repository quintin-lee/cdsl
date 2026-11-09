/**
 * @file test_json_int64.c
 * @brief 64-bit integer (LONG) support for JSON parsing and context loading.
 *
 * Covers: lossless int64 parse, >INT64_MAX overflow protocol (degrade to
 * double, non-int), 32-bit-boundary mapping to CDSL_TYPE_LONG vs INT in
 * object and array branches of load_json_recursive.
 */

#include <cdsl/util/json.h>
#include <cdsl/context.h>
#include <cdsl/ast.h>
#include <stdint.h>
#include <limits.h>
#include "../src/vm/internal.h"
#include "test.h"

/** Parse a bare JSON literal and report how the parser classified it. */
static void
check_num_parse(const char* literal, int expect_is_int,
		int64_t expect_i64, double expect_f64)
{
	cdsl_json_value_t* v = cdsl_json_parse(literal);
	TEST_ASSERT_NOT_NULL(v, literal);
	TEST_ASSERT(v->type == CDSL_JSON_NUMBER, "should be number");
	if (expect_is_int) {
		TEST_ASSERT(v->is_int == 1, "pure integer should be int-marked");
		TEST_ASSERT(v->value.i64 == expect_i64, "i64 value");
	} else {
		TEST_ASSERT(v->is_int == 0, "non-integer / overflow should be float");
		TEST_ASSERT(v->value.f64 == expect_f64, "f64 value");
	}
	cdsl_json_free(v);
}

static void
test_json_int64_parse_boundaries(void)
{
	TEST_BEGIN("JSON int64 parse: boundaries and overflow protocol");

	/* LLONG_MAX: lossless int64, INT-marked */
	check_num_parse("9223372036854775807", 1, INT64_MAX, 0.0);
	/* LLONG_MIN: lossless int64, INT-marked */
	check_num_parse("-9223372036854775808", 1, INT64_MIN, 0.0);
	/* INT64_MAX+1: overflows int64 -> degrade to double, FLOAT-marked */
	check_num_parse("9223372036854775808", 0, 0, 9223372036854775808.0);
	/* Decimal: FLOAT-marked */
	check_num_parse("9.2", 0, 0, 9.2);
	/* Small int: INT-marked, lossless */
	check_num_parse("42", 1, 42, 0.0);
	/* Zero: INT-marked */
	check_num_parse("0", 1, 0, 0.0);
	/* Negative small int: INT-marked */
	check_num_parse("-7", 1, -7, 0.0);
	/* 2^53 (double precision boundary): INT-marked, lossless in i64 */
	check_num_parse("9007199254740992", 1, 9007199254740992LL, 0.0);

	TEST_END();
}

/** Load a JSON object and assert a top-level scalar variable's resolved type. */
static void
assert_ctx_type(const char* json, const char* var, cdsl_type_t expect)
{
	cdsl_context_t* ctx = cdsl_context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx, "ctx");
	TEST_ASSERT(cdsl_context_load_json(ctx, json) == 1, "load ok");
	cdsl_context_entry_t* e = cdsl_context_get_entry_internal(ctx, var);
	TEST_ASSERT_NOT_NULL(e, "var present");
	const cdsl_value_t* v = &e->value;
	TEST_ASSERT(v->type == expect, "var type");
	cdsl_context_free(ctx);
}

static void
test_json_int64_context_mapping(void)
{
	TEST_BEGIN("load_json_recursive: int64 -> LONG vs INT vs FLOAT (object)");

	/* 2147483648 == INT32_MAX+1: exceeds 32-bit int -> LONG */
	assert_ctx_type("{\"x\": 2147483648}", "x", CDSL_TYPE_LONG);
	/* -2147483649 == INT32_MIN-1: exceeds 32-bit int -> LONG */
	assert_ctx_type("{\"x\": -2147483649}", "x", CDSL_TYPE_LONG);
	/* 2147483647 == INT32_MAX: fits 32-bit -> INT */
	assert_ctx_type("{\"x\": 2147483647}", "x", CDSL_TYPE_INT);
	/* -2147483648 == INT32_MIN: fits 32-bit -> INT */
	assert_ctx_type("{\"x\": -2147483648}", "x", CDSL_TYPE_INT);
	/* 42: small int -> INT */
	assert_ctx_type("{\"x\": 42}", "x", CDSL_TYPE_INT);
	/* 9.2: float -> FLOAT */
	assert_ctx_type("{\"x\": 9.2}", "x", CDSL_TYPE_FLOAT);
	/* 9007199254740993 (2^53+1): exceeds 32-bit -> LONG, lossless i64 */
	assert_ctx_type("{\"x\": 9007199254740993}", "x", CDSL_TYPE_LONG);

	TEST_END();
}

static void
test_json_int64_array_mapping(void)
{
	TEST_BEGIN("load_json_recursive: array element int64 -> LONG/INT");

	cdsl_context_t* ctx = cdsl_context_create(NULL);
	TEST_ASSERT_NOT_NULL(ctx, "ctx");
	/* Array: first elem > INT32_MAX (LONG), second small (INT) */
	TEST_ASSERT(cdsl_context_load_json(ctx, "{\"xs\": [10000000000, 42]}") == 1,
		    "load ok");
	cdsl_context_entry_t* xe = cdsl_context_get_entry_internal(ctx, "xs");
	TEST_ASSERT_NOT_NULL(xe, "array present");
	const cdsl_value_t* v = &xe->value;
	TEST_ASSERT(v->type == CDSL_TYPE_ARRAY, "should be array");
	const cdsl_array_t* arr = v->data.array_val;
	TEST_ASSERT_NOT_NULL(arr, "array items");
	TEST_ASSERT(arr->count == 2, "array count");
	TEST_ASSERT(arr->items[0].type == CDSL_TYPE_LONG, "elem0 -> LONG");
	TEST_ASSERT(arr->items[0].data.long_val == 10000000000LL, "elem0 value");
	TEST_ASSERT(arr->items[1].type == CDSL_TYPE_INT, "elem1 -> INT");
	TEST_ASSERT(arr->items[1].data.int_val == 42, "elem1 value");
	cdsl_context_free(ctx);

	/* Array float elements stay FLOAT */
	ctx = cdsl_context_create(NULL);
	TEST_ASSERT(cdsl_context_load_json(ctx, "{\"fs\": [9.2, 1.5]}") == 1, "load ok");
	cdsl_context_entry_t* fe = cdsl_context_get_entry_internal(ctx, "fs");
	TEST_ASSERT_NOT_NULL(fe, "float array present");
	v = &fe->value;
	arr = v->data.array_val;
	TEST_ASSERT(arr->items[0].type == CDSL_TYPE_FLOAT, "float elem0");
	TEST_ASSERT(arr->items[1].type == CDSL_TYPE_FLOAT, "float elem1");
	cdsl_context_free(ctx);

	TEST_END();
}

int
main(void)
{
	printf("Running JSON int64 / LONG mapping tests...\n");
	test_json_int64_parse_boundaries();
	test_json_int64_context_mapping();
	test_json_int64_array_mapping();
	TEST_SUMMARY();
	TEST_EXIT();
}
