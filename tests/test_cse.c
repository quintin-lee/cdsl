/**
 * @file tests/test_cse.c
 * @brief Unit tests for Common Subexpression Elimination and Shared Predicate Cache.
 */

#include "test.h"
#include "cdsl/cdsl.h"
#include "cdsl/cse.h"
#include <stdlib.h>
#include <string.h>

static void
test_cse_ast_hash_and_equal(void)
{
	TEST_BEGIN("cse: structural AST hash and deep equality");

	cdsl_rule_t* r1 = cdsl_parse_string("RULE r1 { WHEN a + b * 2 == 25 THEN pass() }", NULL);
	cdsl_rule_t* r2 = cdsl_parse_string("RULE r2 { WHEN a + b * 2 == 25 THEN pass() }", NULL);
	cdsl_rule_t* r3 = cdsl_parse_string("RULE r3 { WHEN a + b * 3 == 25 THEN pass() }", NULL);
	cdsl_rule_t* r4 = cdsl_parse_string("RULE r4 { WHEN a + b * 2 == 26 THEN pass() }", NULL);

	TEST_ASSERT_NOT_NULL(r1, "parse r1");
	TEST_ASSERT_NOT_NULL(r2, "parse r2");
	TEST_ASSERT_NOT_NULL(r3, "parse r3");
	TEST_ASSERT_NOT_NULL(r4, "parse r4");

	uint64_t h1 = cdsl_expr_hash(r1->when_expr);
	uint64_t h2 = cdsl_expr_hash(r2->when_expr);
	uint64_t h3 = cdsl_expr_hash(r3->when_expr);
	uint64_t h4 = cdsl_expr_hash(r4->when_expr);

	TEST_ASSERT(h1 != 0, "hash is non-zero");
	TEST_ASSERT(h1 == h2, "identical ASTs produce identical hash");
	TEST_ASSERT(h1 != h3, "different multiplier produces different hash");
	TEST_ASSERT(h1 != h4, "different constant produces different hash");

	TEST_ASSERT_INT(cdsl_expr_equal(r1->when_expr, r2->when_expr), 1, "r1 == r2");
	TEST_ASSERT_INT(cdsl_expr_equal(r1->when_expr, r3->when_expr), 0, "r1 != r3");
	TEST_ASSERT_INT(cdsl_expr_equal(r1->when_expr, r4->when_expr), 0, "r1 != r4");

	cdsl_free_rule(r1);
	cdsl_free_rule(r2);
	cdsl_free_rule(r3);
	cdsl_free_rule(r4);
	TEST_END();
}

static void
test_cse_cache_lifecycle(void)
{
	TEST_BEGIN("cse: predicate cache standalone get/put/clear");
	cdsl_pred_cache_t* cache = cdsl_pred_cache_create(16);
	TEST_ASSERT_NOT_NULL(cache, "create cache");

	cdsl_rule_t* r = cdsl_parse_string("RULE r { WHEN score >= 60 THEN pass() }", NULL);
	TEST_ASSERT_NOT_NULL(r, "parse rule");

	cdsl_value_t lookup;
	TEST_ASSERT_INT(
	    cdsl_pred_cache_get(cache, r->when_expr, &lookup), 0, "cache miss initially");
	TEST_ASSERT_INT((int)cache->misses, 1, "1 miss");
	TEST_ASSERT_INT((int)cache->hits, 0, "0 hits");

	cdsl_value_t v = {.type = CDSL_TYPE_BOOL, .data.bool_val = 1};
	TEST_ASSERT_INT(cdsl_pred_cache_put(cache, r->when_expr, &v), 1, "cache put");
	TEST_ASSERT_INT((int)cache->count, 1, "1 entry stored");

	TEST_ASSERT_INT(cdsl_pred_cache_get(cache, r->when_expr, &lookup), 1, "cache hit");
	TEST_ASSERT_INT((int)cache->hits, 1, "1 hit");
	TEST_ASSERT_INT(lookup.type, CDSL_TYPE_BOOL, "type is bool");
	TEST_ASSERT_INT(lookup.data.bool_val, 1, "value is true");
	cdsl_value_free(&lookup);

	cdsl_pred_cache_clear(cache);
	TEST_ASSERT_INT((int)cache->count, 0, "cache count 0 after clear");
	TEST_ASSERT_INT(cdsl_pred_cache_get(cache, r->when_expr, &lookup), 0, "miss after clear");

	cdsl_free_rule(r);
	cdsl_pred_cache_free(cache);
	TEST_END();
}

static void
test_cse_vm_shared_eval(void)
{
	TEST_BEGIN("cse: VM predicate cache sharing across rules");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "score", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "country", CDSL_TYPE_STRING);
	cdsl_schema_register_action(schema, "action_a", CDSL_TYPE_VOID, 0);
	cdsl_schema_register_action(schema, "action_b", CDSL_TYPE_VOID, 0);

	cdsl_rule_t* r1 = cdsl_parse_string(
	    "RULE r1 { WHEN score >= 80 AND country == \"US\" THEN action_a() }", NULL);
	cdsl_rule_t* r2 = cdsl_parse_string(
	    "RULE r2 { WHEN score >= 80 AND country == \"DE\" THEN action_b() }", NULL);
	TEST_ASSERT_NOT_NULL(r1, "parse r1");
	TEST_ASSERT_NOT_NULL(r2, "parse r2");

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_vm_enable_pred_cache(vm, 1);
	cdsl_pred_cache_t* cache = cdsl_vm_get_pred_cache(vm);
	TEST_ASSERT_NOT_NULL(cache, "pred cache enabled on VM");

	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_int(ctx, "score", 90);
	cdsl_context_set_string(ctx, "country", "US");

	/* Evaluate rule 1: score >= 80 is true, country == US is true */
	cdsl_rule_report_t* rpt1 = cdsl_vm_execute(vm, r1, ctx);
	TEST_ASSERT_NOT_NULL(rpt1, "execute r1");
	TEST_ASSERT_INT(rpt1->status, CDSL_STATUS_FAILED, "r1 triggered");

	/* Evaluate rule 2: score >= 80 was cached from r1! */
	cdsl_rule_report_t* rpt2 = cdsl_vm_execute(vm, r2, ctx);
	TEST_ASSERT_NOT_NULL(rpt2, "execute r2");
	TEST_ASSERT_INT(rpt2->status, CDSL_STATUS_PASSED, "r2 did not trigger");

	TEST_ASSERT(cache->hits > 0, "predicate cache recorded hits");

	cdsl_report_free(rpt1);
	cdsl_report_free(rpt2);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_free_rule(r1);
	cdsl_free_rule(r2);
	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_cse_ruleset_batch(void)
{
	TEST_BEGIN("cse: ruleset batch execution automatically shares predicate cache");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "tier", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "grant", CDSL_TYPE_VOID, 0);

	cdsl_ruleset_t* rs = cdsl_ruleset_create();
	for (int i = 0; i < 5; i++) {
		cdsl_rule_t* r = cdsl_parse_string("RULE r { WHEN tier >= 3 THEN grant() }", NULL);
		cdsl_ruleset_add(rs, r, i);
	}

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_int(ctx, "tier", 4);

	cdsl_ruleset_report_t* rep = cdsl_vm_execute_ruleset(vm, rs, ctx);
	TEST_ASSERT_NOT_NULL(rep, "batch execution report");
	TEST_ASSERT_INT(rep->total_failed, 5, "all 5 rules evaluated condition as true");

	cdsl_ruleset_report_free(rep);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_ruleset_free(rs);
	cdsl_schema_free(schema);
	TEST_END();
}

static void
test_cse_context_invalidation(void)
{
	TEST_BEGIN("cse: context mutation hook invalidates predicate cache");
	cdsl_pred_cache_t* cache = cdsl_pred_cache_create(16);
	TEST_ASSERT_NOT_NULL(cache, "create cache");

	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "score", CDSL_TYPE_INT);
	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_int(ctx, "score", 50);

	TEST_ASSERT_INT(cdsl_pred_cache_bind_context(cache, ctx), 1, "bind context");

	cdsl_rule_t* r = cdsl_parse_string("RULE r { WHEN score >= 60 THEN pass() }", NULL);
	TEST_ASSERT_NOT_NULL(r, "parse rule");

	cdsl_value_t v = {.type = CDSL_TYPE_BOOL, .data.bool_val = 1};
	TEST_ASSERT_INT(cdsl_pred_cache_put(cache, r->when_expr, &v), 1, "cache put");
	TEST_ASSERT_INT((int)cache->count, 1, "cache count is 1");

	/* Modifying context variable must automatically invalidate cache */
	cdsl_context_set_int(ctx, "score", 95);
	TEST_ASSERT_INT((int)cache->count, 0, "cache count is 0 after context update");

	/* Put again, then remove variable */
	TEST_ASSERT_INT(cdsl_pred_cache_put(cache, r->when_expr, &v), 1, "cache put again");
	TEST_ASSERT_INT((int)cache->count, 1, "cache count is 1");
	cdsl_context_remove(ctx, "score");
	TEST_ASSERT_INT((int)cache->count, 0, "cache count is 0 after context remove");

	/* Unbind context; subsequent mutations should not clear cache */
	cdsl_pred_cache_unbind_context(cache, ctx);
	TEST_ASSERT_INT(cdsl_pred_cache_put(cache, r->when_expr, &v), 1, "cache put while unbound");
	TEST_ASSERT_INT((int)cache->count, 1, "cache count is 1");
	cdsl_context_set_int(ctx, "score", 88);
	TEST_ASSERT_INT((int)cache->count, 1, "cache count remains 1 while unbound");

	cdsl_pred_cache_bind_context(cache, ctx);
	cdsl_free_rule(r);
	cdsl_pred_cache_free(cache);
	cdsl_context_free(ctx);
	cdsl_schema_free(schema);
	TEST_END();
}

int
main(void)
{
	printf("Running Common Subexpression Elimination & Predicate Cache tests...\n");
	test_cse_ast_hash_and_equal();
	test_cse_cache_lifecycle();
	test_cse_context_invalidation();
	test_cse_vm_shared_eval();
	test_cse_ruleset_batch();
	TEST_SUMMARY();
	TEST_EXIT();
}
