#include "test.h"
#include "cdsl/cdsl.h"
#include "cdsl/schema.h"

static void
test_range_contradiction_inference(void)
{
	TEST_BEGIN("static check - contradictory range [20..10]");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "x", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "log", CDSL_TYPE_VOID, 1, CDSL_TYPE_INT);

	const char* dsl = "RULE r { WHEN x in [20..10] THEN log(1) }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "parse rule");

	char err[512] = {0};
	bool ok = cdsl_verify_rule(rule, schema, err, sizeof(err));
	TEST_ASSERT(!ok, "contradictory range [20..10] must fail validation");
	TEST_ASSERT(strstr(err, "Contradictory range") != NULL,
		    "error mentions Contradictory range");

	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_range_valid_inference(void)
{
	TEST_BEGIN("static check - valid range [10..20]");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "x", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "log", CDSL_TYPE_VOID, 1, CDSL_TYPE_INT);

	const char* dsl = "RULE r { WHEN x in [10..20] THEN log(1) }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "parse rule");

	char err[512] = {0};
	bool ok = cdsl_verify_rule(rule, schema, err, sizeof(err));
	TEST_ASSERT(ok, "valid range [10..20] passes schema verification");

	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_subscript_type_inference(void)
{
	TEST_BEGIN("static check - subscripting non-collection");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "x", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "log", CDSL_TYPE_VOID, 1, CDSL_TYPE_INT);

	const char* dsl = "RULE r { WHEN x[0] == 1 THEN log(1) }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "parse rule");

	char err[512] = {0};
	bool ok = cdsl_verify_rule(rule, schema, err, sizeof(err));
	TEST_ASSERT(!ok, "subscripting int variable must fail verification");
	TEST_ASSERT(strstr(err, "Subscript") != NULL, "error mentions Subscript");

	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_map_schema_validation(void)
{
	TEST_BEGIN("static check - map property validation");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user", CDSL_TYPE_MAP);
	cdsl_schema_register_action(schema, "log", CDSL_TYPE_VOID, 1, CDSL_TYPE_INT);

	const char* dsl = "RULE r { WHEN user.age > 18 THEN log(1) }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "parse rule");

	char err[512] = {0};
	bool ok = cdsl_verify_rule(rule, schema, err, sizeof(err));
	TEST_ASSERT(ok, "user.age on map passes schema verification");

	cdsl_free_rule(rule);
	cdsl_schema_free(schema);
	TEST_END();
}

int
main(void)
{
	printf("Running type inference tests...\n");
	test_range_contradiction_inference();
	test_range_valid_inference();
	test_subscript_type_inference();
	test_map_schema_validation();
	TEST_SUMMARY();
	TEST_EXIT();
}
