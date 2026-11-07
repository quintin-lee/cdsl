#include "test.h"
#include "cdsl/cdsl.h"
#include "cdsl/bytecode.h"
#include <string.h>

static void
test_map_c_api_basic(void)
{
	TEST_BEGIN("map C API - basic set/get/has");
	cdsl_map_t* map = cdsl_map_create(8);
	TEST_ASSERT_NOT_NULL(map, "create map");

	cdsl_value_t v_int;
	v_int.type = CDSL_TYPE_INT;
	v_int.data.int_val = 42;
	v_int.owned_string = 0;
	cdsl_map_set(map, "age", &v_int);

	cdsl_value_t v_str;
	v_str.type = CDSL_TYPE_STRING;
	v_str.data.string_val = cdsl_strdup("alice");
	v_str.owned_string = 1;
	cdsl_map_set(map, "name", &v_str);
	cdsl_value_free(&v_str);

	TEST_ASSERT(cdsl_map_has(map, "age"), "has age");
	TEST_ASSERT(cdsl_map_has(map, "name"), "has name");
	TEST_ASSERT(!cdsl_map_has(map, "unknown"), "not has unknown");

	const cdsl_value_t* r_age = cdsl_map_get(map, "age");
	TEST_ASSERT_NOT_NULL(r_age, "get age");
	TEST_ASSERT_INT(r_age->type, CDSL_TYPE_INT, "age type int");
	TEST_ASSERT_INT(r_age->data.int_val, 42, "age val 42");

	const cdsl_value_t* r_name = cdsl_map_get(map, "name");
	TEST_ASSERT_NOT_NULL(r_name, "get name");
	TEST_ASSERT_INT(r_name->type, CDSL_TYPE_STRING, "name type string");
	TEST_ASSERT_STR(r_name->data.string_val, "alice", "name val alice");

	cdsl_map_free(map);
	TEST_END();
}

static void
test_map_c_api_nested(void)
{
	TEST_BEGIN("map C API - nested maps & copy");
	cdsl_map_t* inner = cdsl_map_create(4);
	cdsl_value_t v_city;
	v_city.type = CDSL_TYPE_STRING;
	v_city.data.string_val = cdsl_strdup("Paris");
	v_city.owned_string = 1;
	cdsl_map_set(inner, "city", &v_city);
	cdsl_value_free(&v_city);

	cdsl_map_t* outer = cdsl_map_create(4);
	cdsl_value_t v_addr;
	v_addr.type = CDSL_TYPE_MAP;
	v_addr.data.map_val = inner;
	v_addr.owned_string = 0;
	cdsl_map_set(outer, "address", &v_addr);
	cdsl_map_free(inner);

	// Test deep copy
	cdsl_map_t* copy = cdsl_map_copy(outer);
	TEST_ASSERT_NOT_NULL(copy, "map copy");
	TEST_ASSERT(cdsl_map_has(copy, "address"), "copy has address");

	const cdsl_value_t* c_addr = cdsl_map_get(copy, "address");
	TEST_ASSERT_NOT_NULL(c_addr, "copy get address");
	TEST_ASSERT_INT(c_addr->type, CDSL_TYPE_MAP, "copy address is map");
	const cdsl_value_t* c_city = cdsl_map_get(c_addr->data.map_val, "city");
	TEST_ASSERT_NOT_NULL(c_city, "copy get city");
	TEST_ASSERT_STR(c_city->data.string_val, "Paris", "copy city is Paris");

	cdsl_map_free(outer);
	cdsl_map_free(copy);
	TEST_END();
}

static void
test_map_context_integration(void)
{
	TEST_BEGIN("map context dot path navigation");
	cdsl_context_t* ctx = cdsl_context_create(NULL);
	cdsl_map_t* user = cdsl_map_create(4);
	cdsl_map_t* profile = cdsl_map_create(4);

	cdsl_value_t v_score;
	v_score.type = CDSL_TYPE_INT;
	v_score.data.int_val = 99;
	v_score.owned_string = 0;
	cdsl_map_set(profile, "score", &v_score);

	cdsl_value_t v_prof;
	v_prof.type = CDSL_TYPE_MAP;
	v_prof.data.map_val = profile;
	v_prof.owned_string = 0;
	cdsl_map_set(user, "profile", &v_prof);
	cdsl_map_free(profile);

	cdsl_context_set_map(ctx, "user", user);

	int score = cdsl_context_get_int(ctx, "user.profile.score", 0);
	TEST_ASSERT_INT(score, 99, "score value is 99");

	cdsl_context_free(ctx);
	TEST_END();
}

static void
test_map_dsl_literal_subscript(void)
{
	TEST_BEGIN("map DSL literal and subscript access");
	const char* dsl = "RULE map_test {\n"
			  "  WHEN { \"id\": 101, \"active\": true }[\"id\"] == 101 THEN pass()\n"
			  "}";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "parse map literal subscript rule");

	cdsl_context_t* ctx = cdsl_context_create(NULL);
	cdsl_vm_t* vm = cdsl_vm_create(NULL);

	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute report");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "condition is true -> FAILED");

	cdsl_report_free(rpt);
	cdsl_vm_free(vm);
	cdsl_context_free(ctx);
	cdsl_free_rule(rule);
	TEST_END();
}

static void
test_map_dsl_dot_property(void)
{
	TEST_BEGIN("map DSL dot property access");
	const char* dsl = "RULE map_dot_test {\n"
			  "  WHEN user.age >= 18 THEN pass()\n"
			  "}";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "parse dot property rule");

	cdsl_context_t* ctx = cdsl_context_create(NULL);
	cdsl_map_t* user = cdsl_map_create(4);
	cdsl_value_t v_age;
	v_age.type = CDSL_TYPE_INT;
	v_age.data.int_val = 20;
	v_age.owned_string = 0;
	cdsl_map_set(user, "age", &v_age);
	cdsl_context_set_map(ctx, "user", user);

	cdsl_vm_t* vm = cdsl_vm_create(NULL);
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute report");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "user.age >= 18 is true");

	cdsl_report_free(rpt);
	cdsl_vm_free(vm);
	cdsl_context_free(ctx);
	cdsl_free_rule(rule);
	TEST_END();
}

static void
test_map_bytecode_execution(void)
{
	TEST_BEGIN("map bytecode engine execution");
	const char* dsl = "RULE bc_map_test {\n"
			  "  WHEN { \"level\": 5 }[\"level\"] == 5 THEN pass()\n"
			  "}";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	TEST_ASSERT_NOT_NULL(rule, "parse bytecode map rule");

	cdsl_bytecode_t bc = {0};
	int ok = cdsl_bytecode_compile(rule, NULL, &bc);
	TEST_ASSERT_INT(ok, 1, "compile to bytecode");

	cdsl_vm_t* vm = cdsl_vm_create(NULL);
	cdsl_context_t* ctx = cdsl_context_create(NULL);
	cdsl_value_t res = cdsl_bytecode_execute(vm, &bc, ctx);
	TEST_ASSERT_INT(res.data.bool_val, 1, "bytecode map subscript matches");

	cdsl_bytecode_free(&bc);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_free_rule(rule);
	TEST_END();
}

int
main(void)
{
	printf("Running map tests...\n");
	test_map_c_api_basic();
	test_map_c_api_nested();
	test_map_context_integration();
	test_map_dsl_literal_subscript();
	test_map_dsl_dot_property();
	test_map_bytecode_execution();
	TEST_SUMMARY();
	TEST_EXIT();
}
