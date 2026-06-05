/**
 * @file test_template.c
 * @brief Tests for rule template registration, inheritance, and management.
 */

#include "test.h"
#include <cdsl/cdsl.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static void
test_template_register_simple(void)
{
	TEST_BEGIN("register simple template");
	cdsl_template_clear(); // Start clean

	const char* dsl =
	    "TEMPLATE base { "
	    "  METRIC m { META { weight = \"50\" } CASE 1 == 1 THEN score(50) DEFAULT score(0) } "
	    "}";
	cdsl_rule_t* tpl = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(tpl, "Template should parse");
	cdsl_template_register(tpl);

	// Verify it's retrievable (registry stores shallow pointer, do not free tpl)
	const cdsl_rule_t* retrieved = cdsl_template_get("base");
	TEST_ASSERT_NOT_NULL(retrieved, "Template should be retrievable");
	TEST_ASSERT_STR(retrieved->name, "base", "Template name matches");

	TEST_END();
}

static void
test_template_unknown_returns_null(void)
{
	TEST_BEGIN("unknown template returns NULL");
	const cdsl_rule_t* t = cdsl_template_get("nonexistent_template_xyz");
	TEST_ASSERT_NULL(t, "Unknown template returns NULL");
	TEST_END();
}

static void
test_template_extends_execution(void)
{
	TEST_BEGIN("rule EXTENDS template execution");
	cdsl_template_clear();

	// Register base template
	const char* tpl_dsl =
	    "TEMPLATE base { META { description = \"base\" } "
	    "METRIC m1 { META { weight = \"50\" } CASE 1 == 1 THEN score(50) DEFAULT score(0) } }";
	cdsl_rule_t* tpl = cdsl_parse_string(tpl_dsl, NULL);
	TEST_ASSERT_NOT_NULL(tpl, "Template should parse");
	cdsl_template_register(tpl);

	// Create rule that extends template (registry stores shallow pointer, do not free tpl)
	const char* dsl =
	    "RULE r EXTENDS base { "
	    "METRIC m2 { META { weight = \"50\" } CASE 1 == 1 THEN score(50) DEFAULT score(0) } }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "Extended rule should parse");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "Report not NULL");
	// Both metrics pass (CASE 1==1 matched), score = 50+50 = 100
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "Both metrics pass -> PASSED");

	cdsl_report_free(rpt);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	cdsl_free_rule(rule);
	TEST_END();
}

static void
test_template_clear(void)
{
	TEST_BEGIN("template clear");
	cdsl_template_clear();

	// Register one
	const char* dsl =
	    "TEMPLATE t { METRIC m { CASE 1 == 1 THEN score(100) DEFAULT score(0) } }";
	cdsl_rule_t* tpl = cdsl_parse_string(dsl, NULL);
	cdsl_template_register(tpl);
	// Do NOT free tpl — registry stores shallow pointer
	TEST_ASSERT_NOT_NULL(cdsl_template_get("t"), "Template exists after register");

	// Clear and verify
	cdsl_template_clear();
	TEST_ASSERT_NULL(cdsl_template_get("t"), "Template gone after clear");

	TEST_END();
}

static void
test_template_reuse_across_rules(void)
{
	TEST_BEGIN("template reused across multiple rules");
	cdsl_template_clear();

	// Register a shared template
	const char* tpl_dsl =
	    "TEMPLATE shared { META { description = \"shared\" } "
	    "METRIC m { META { weight = \"100\" } CASE 1 == 1 THEN score(100) DEFAULT score(0) } }";
	cdsl_rule_t* tpl = cdsl_parse_string(tpl_dsl, NULL);
	cdsl_template_register(tpl);
	// Do NOT free tpl — registry stores shallow pointer

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	// Rule A extends shared (with META block only)
	cdsl_rule_t* rule_a =
	    cdsl_parse_string("RULE a EXTENDS shared { META { description = \"A\" } }", NULL);
	TEST_ASSERT_NOT_NULL(rule_a, "Rule A should parse");

	cdsl_rule_report_t* rpt_a = cdsl_vm_execute(vm, rule_a, ctx);
	TEST_ASSERT_INT(rpt_a->status, CDSL_STATUS_PASSED, "Rule A passes");
	cdsl_report_free(rpt_a);
	cdsl_free_rule(rule_a);

	// Rule B also extends shared
	cdsl_rule_t* rule_b =
	    cdsl_parse_string("RULE b EXTENDS shared { META { description = \"B\" } }", NULL);
	TEST_ASSERT_NOT_NULL(rule_b, "Rule B should parse");

	cdsl_rule_report_t* rpt_b = cdsl_vm_execute(vm, rule_b, ctx);
	TEST_ASSERT_INT(rpt_b->status, CDSL_STATUS_PASSED, "Rule B passes");
	cdsl_report_free(rpt_b);
	cdsl_free_rule(rule_b);

	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}

int
main()
{
	printf("Running template tests...\n");
	test_template_register_simple();
	test_template_unknown_returns_null();
	test_template_extends_execution();
	test_template_clear();
	test_template_reuse_across_rules();
	TEST_SUMMARY();
	TEST_EXIT();
}
