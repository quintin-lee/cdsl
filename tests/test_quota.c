#include "test.h"
#include "cdsl/cdsl.h"

void
test_instruction_quota(void)
{
	TEST_BEGIN("instruction quota");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	// A rule that takes more than 10 instructions to execute
	// 1+2 = 3 instructions (PUSH, PUSH, ADD)
	// 3+3 = 2 instructions (PUSH, ADD)
	// ...
	cdsl_rule_t* rule = cdsl_parse_string(
	    "RULE r { WHEN 1 + 2 + 3 + 4 + 5 + 6 + 7 == 28 THEN block(\"ok\") }", NULL);

	cdsl_vm_set_instruction_limit(vm, 5); // Very low limit
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);

	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_ERROR, "Should abort due to instruction quota");

	cdsl_report_free(rpt);
	cdsl_free_rule(rule);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

int
main()
{
	test_instruction_quota();
	return 0;
}
