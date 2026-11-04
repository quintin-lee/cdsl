/**
 * @file test_runtime_errors.c
 * @brief Runtime error capture tests for C-DSL (Subproject D1).
 *
 * Verifies that runtime errors (instruction quota exceeded, etc.) are
 * captured with full detail and exposed via cdsl_vm_get_last_error()
 * and cdsl_rule_report_t.error.
 */

#include <cdsl/api.h>
#include <cdsl/util/error.h>
#include <string.h>
#include "test.h"

static void
test_runtime_error_instruction_quota(void)
{
	TEST_BEGIN("runtime error: instruction quota exceeded");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "x", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 0, CDSL_TYPE_VOID);

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	/* Set a very low instruction limit so execution always aborts */
	cdsl_vm_set_instruction_limit(vm, 1);

	const char* dsl = "RULE test_rule {\n"
			  "  WHEN x > 0 THEN block()\n"
			  "}";
	cdsl_error_list_t* parse_errors = NULL;
	cdsl_rule_t* rule = cdsl_parse_string(dsl, &parse_errors);
	if (parse_errors) {
		cdsl_error_list_free(parse_errors);
	}
	TEST_ASSERT_NOT_NULL(rule, "rule should parse");

	cdsl_context_set_int(ctx, "x", 1);

	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "report should not be NULL");
	TEST_ASSERT(rpt->status == CDSL_STATUS_ERROR, "status should be ERROR");

	/* VM last_error should be set (transferred to report during execute) */
	const cdsl_error_t* err = rpt->error;
	TEST_ASSERT_NOT_NULL(err, "report->error should not be NULL");
	TEST_ASSERT(err->kind == CDSL_ERR_RUNTIME, "kind should be RUNTIME");
	TEST_ASSERT(strstr(err->message, "quota") != NULL, "message should mention quota");

	/* Report should also carry the error */
	TEST_ASSERT_NOT_NULL(rpt->error, "report->error should not be NULL");
	TEST_ASSERT(rpt->error->kind == CDSL_ERR_RUNTIME, "report error kind should be RUNTIME");

	cdsl_report_free(rpt);
	cdsl_free_rule(rule);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);

	TEST_END();
}

static void
test_runtime_error_no_error(void)
{
	TEST_BEGIN("runtime error: no error on success");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "x", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 0, CDSL_TYPE_VOID);

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_vm_set_instruction_limit(vm, 1000);

	const char* dsl = "RULE test_rule {\n"
			  "  WHEN x > 0 THEN block()\n"
			  "}";
	cdsl_error_list_t* parse_errors = NULL;
	cdsl_rule_t* rule = cdsl_parse_string(dsl, &parse_errors);
	if (parse_errors) {
		cdsl_error_list_free(parse_errors);
	}
	TEST_ASSERT_NOT_NULL(rule, "rule should parse");

	cdsl_context_set_int(ctx, "x", 1);

	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "report should not be NULL");
	TEST_ASSERT(rpt->status != CDSL_STATUS_ERROR, "status should not be ERROR");

	const cdsl_error_t* err = cdsl_vm_get_last_error(vm);
	TEST_ASSERT_NULL(err, "last_error should be NULL when no error");

	TEST_ASSERT_NULL(rpt->error, "report->error should be NULL when no error");

	cdsl_report_free(rpt);
	cdsl_free_rule(rule);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);

	TEST_END();
}

int
main(void)
{
	printf("Running runtime error tests...\n");

	test_runtime_error_instruction_quota();
	test_runtime_error_no_error();

	TEST_SUMMARY();
	TEST_EXIT();
}
