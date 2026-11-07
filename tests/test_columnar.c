/**
 * @file tests/test_columnar.c
 * @brief Unit tests for the vectorized columnar batch evaluation engine.
 */

#include "test.h"
#include "cdsl/cdsl.h"
#include "cdsl/columnar.h"
#include <stdlib.h>
#include <string.h>

static void
test_columnar_basic_lifecycle(void)
{
	TEST_BEGIN("columnar: context lifecycle and column types");
	cdsl_columnar_ctx_t* ctx = cdsl_columnar_create(5);
	TEST_ASSERT_NOT_NULL(ctx, "create columnar context");
	TEST_ASSERT_INT((int)ctx->row_count, 5, "row count is 5");

	int32_t ints[5] = {10, 20, 30, 40, 50};
	int64_t longs[5] = {100L, 200L, 300L, 400L, 500L};
	double floats[5] = {1.5, 2.5, 3.5, 4.5, 5.5};
	uint8_t bools[5] = {1, 0, 1, 0, 1};
	const char* strings[5] = {"apple", "banana", "cherry", "date", "elderberry"};

	TEST_ASSERT_INT(cdsl_columnar_add_int(ctx, "ints", ints), 1, "add int col");
	TEST_ASSERT_INT(cdsl_columnar_add_long(ctx, "longs", longs), 1, "add long col");
	TEST_ASSERT_INT(cdsl_columnar_add_float(ctx, "floats", floats), 1, "add float col");
	TEST_ASSERT_INT(cdsl_columnar_add_bool(ctx, "bools", bools), 1, "add bool col");
	TEST_ASSERT_INT(cdsl_columnar_add_string(ctx, "strings", strings), 1, "add string col");

	const cdsl_column_t* c_int = cdsl_columnar_get(ctx, "ints");
	TEST_ASSERT_NOT_NULL(c_int, "get int col");
	TEST_ASSERT_INT(c_int->type, CDSL_COL_INT, "type is int");
	TEST_ASSERT_INT(c_int->values.int_data[2], 30, "value[2] is 30");

	const cdsl_column_t* c_str = cdsl_columnar_get(ctx, "strings");
	TEST_ASSERT_NOT_NULL(c_str, "get str col");
	TEST_ASSERT_INT(strcmp(c_str->values.string_data[1], "banana"), 0, "value[1] is banana");

	TEST_ASSERT_NULL(cdsl_columnar_get(ctx, "nonexistent"), "nonexistent col is NULL");

	cdsl_columnar_free(ctx);
	TEST_END();
}

static void
test_columnar_numeric_eval(void)
{
	TEST_BEGIN("columnar: numeric comparisons and interval ranges");
	const size_t N = 6;
	cdsl_columnar_ctx_t* ctx = cdsl_columnar_create(N);

	int32_t ages[6] = {16, 17, 18, 19, 21, 65};
	int32_t scores[6] = {45, 60, 75, 85, 95, 100};
	cdsl_columnar_add_int(ctx, "age", ages);
	cdsl_columnar_add_int(ctx, "score", scores);

	uint8_t matches[6];

	/* Rule 1: age >= 18 */
	cdsl_rule_t* r1 = cdsl_parse_string("RULE r1 { WHEN age >= 18 THEN pass() }", NULL);
	TEST_ASSERT_NOT_NULL(r1, "parse r1");
	int64_t count1 = cdsl_columnar_eval_rule(r1, ctx, matches);
	TEST_ASSERT_INT((int)count1, 4, "4 matches for age >= 18");
	TEST_ASSERT_INT(matches[0], 0, "age 16 is false");
	TEST_ASSERT_INT(matches[1], 0, "age 17 is false");
	TEST_ASSERT_INT(matches[2], 1, "age 18 is true");
	TEST_ASSERT_INT(matches[3], 1, "age 19 is true");
	TEST_ASSERT_INT(matches[4], 1, "age 21 is true");
	TEST_ASSERT_INT(matches[5], 1, "age 65 is true");
	cdsl_free_rule(r1);

	/* Rule 2: score in [60..90] */
	cdsl_rule_t* r2 = cdsl_parse_string("RULE r2 { WHEN score in [60..90] THEN pass() }", NULL);
	TEST_ASSERT_NOT_NULL(r2, "parse r2");
	int64_t count2 = cdsl_columnar_eval_rule(r2, ctx, matches);
	TEST_ASSERT_INT((int)count2, 3, "3 matches for score in [60..90]");
	TEST_ASSERT_INT(matches[0], 0, "45 not in range");
	TEST_ASSERT_INT(matches[1], 1, "60 in range");
	TEST_ASSERT_INT(matches[2], 1, "75 in range");
	TEST_ASSERT_INT(matches[3], 1, "85 in range");
	TEST_ASSERT_INT(matches[4], 0, "95 not in range");
	TEST_ASSERT_INT(matches[5], 0, "100 not in range");
	cdsl_free_rule(r2);

	cdsl_columnar_free(ctx);
	TEST_END();
}

static void
test_columnar_string_and_logical(void)
{
	TEST_BEGIN("columnar: string matching and logical operators");
	const size_t N = 4;
	cdsl_columnar_ctx_t* ctx = cdsl_columnar_create(N);

	const char* countries[4] = {"US", "CN", "US", "DE"};
	int32_t amounts[4] = {100, 200, 50, 500};
	uint8_t flags[4] = {1, 0, 0, 1};

	cdsl_columnar_add_string(ctx, "country", countries);
	cdsl_columnar_add_int(ctx, "amount", amounts);
	cdsl_columnar_add_bool(ctx, "vip", flags);

	uint8_t matches[4];

	/* Rule: (country == "US" AND amount > 80) OR vip == true */
	cdsl_rule_t* rule = cdsl_parse_string(
	    "RULE r { WHEN (country == \"US\" AND amount > 80) OR vip == true THEN pass() }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "parse compound rule");

	int64_t cnt = cdsl_columnar_eval_rule(rule, ctx, matches);
	/*
	   Row 0: US, 100, vip=1 -> (T AND T) OR T -> 1
	   Row 1: CN, 200, vip=0 -> (F AND T) OR F -> 0
	   Row 2: US, 50, vip=0  -> (T AND F) OR F -> 0
	   Row 3: DE, 500, vip=1 -> (F AND T) OR T -> 1
	*/
	TEST_ASSERT_INT((int)cnt, 2, "2 matches for compound condition");
	TEST_ASSERT_INT(matches[0], 1, "row 0 matched");
	TEST_ASSERT_INT(matches[1], 0, "row 1 not matched");
	TEST_ASSERT_INT(matches[2], 0, "row 2 not matched");
	TEST_ASSERT_INT(matches[3], 1, "row 3 matched");

	cdsl_free_rule(rule);
	cdsl_columnar_free(ctx);
	TEST_END();
}

static void
test_columnar_batch_equivalence(void)
{
	TEST_BEGIN("columnar: large batch equivalence with row VM");
	const size_t N = 10000;
	cdsl_columnar_ctx_t* ctx = cdsl_columnar_create(N);

	int32_t* age_data = malloc(N * sizeof(int32_t));
	int32_t* score_data = malloc(N * sizeof(int32_t));
	uint8_t* out_columnar = malloc(N * sizeof(uint8_t));

	for (size_t i = 0; i < N; i++) {
		age_data[i] = (int32_t)(i % 100);
		score_data[i] = (int32_t)((i * 17) % 101);
	}

	cdsl_columnar_add_int(ctx, "age", age_data);
	cdsl_columnar_add_int(ctx, "score", score_data);

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "age", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "score", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "pass", CDSL_TYPE_VOID, 0);

	cdsl_rule_t* rule = cdsl_parse_string(
	    "RULE filter { WHEN age >= 18 AND score in [60..100] THEN pass() }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "parse rule");

	int64_t col_matches = cdsl_columnar_eval_rule(rule, ctx, out_columnar);
	TEST_ASSERT_INT(col_matches > 0, 1, "columnar evaluated matches");

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* row_ctx = cdsl_context_create(schema);

	int64_t row_matches = 0;
	int mismatch_found = 0;
	for (size_t i = 0; i < N; i++) {
		cdsl_context_set_int(row_ctx, "age", age_data[i]);
		cdsl_context_set_int(row_ctx, "score", score_data[i]);
		cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, row_ctx);
		uint8_t row_matched = (rpt && rpt->status == CDSL_STATUS_FAILED) ? 1 : 0;
		row_matches += row_matched;
		if (row_matched != out_columnar[i]) {
			mismatch_found = 1;
			cdsl_report_free(rpt);
			break;
		}
		cdsl_report_free(rpt);
	}

	TEST_ASSERT_INT(mismatch_found, 0, "No row mismatches between columnar and VM execution");
	TEST_ASSERT_INT((int)col_matches, (int)row_matches, "10,000 rows exactly match");

	cdsl_context_free(row_ctx);
	cdsl_vm_free(vm);
	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	cdsl_columnar_free(ctx);
	free(age_data);
	free(score_data);
	free(out_columnar);
	TEST_END();
}

int
main(void)
{
	printf("Running vectorized columnar batch engine tests...\n");
	test_columnar_basic_lifecycle();
	test_columnar_numeric_eval();
	test_columnar_string_and_logical();
	test_columnar_batch_equivalence();
	TEST_SUMMARY();
	TEST_EXIT();
}
