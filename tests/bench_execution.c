/**
 * @file tests/bench_execution.c
 * @brief Performance benchmark for the C-DSL execution engine.
 *
 * Measures and reports baseline performance for key operations:
 * - Simple rule parsing
 * - Simple rule execution (tree-walk vs bytecode)
 * - Metric-based scoring rule execution
 * - RuleSet batch execution
 * - Parallel RuleSet execution
 */

#include "cdsl/cdsl.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

/** @brief Number of iterations for each benchmark. */
#define BENCH_ITERATIONS 10000

/** @brief Get current time in microseconds. */
static double
get_time_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

/** @brief Report a benchmark result. */
static void
report(const char* name, double elapsed_us, int iterations)
{
	double avg = elapsed_us / iterations;
	printf("  %-40s %8.2f us/op  (%10.2f ms total, %d iters)\n",
	       name, avg, elapsed_us / 1000.0, iterations);
}

int
main(void)
{
	printf("========================================\n");
	printf("  C-DSL Performance Benchmarks\n");
	printf("========================================\n\n");

	/* ---- Setup ---- */
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
	cdsl_schema_register_var(schema, "user.score", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	cdsl_schema_register_action(schema, "score", CDSL_TYPE_VOID, 1, CDSL_TYPE_INT);

	const char* simple_dsl =
	    "RULE simple { WHEN user.age >= 18 THEN block(\"adult\") }";
	const char* metric_dsl = "RULE metric { "
				   "METRIC m1 { "
				   "META { weight = \"50\" } "
				   "CASE user.score >= 80 THEN score(50) "
				   "CASE user.score >= 50 THEN score(30) "
				   "DEFAULT score(0) "
				   "} "
				   "}";

	cdsl_rule_t* simple_rule = cdsl_parse_string(simple_dsl, NULL);
	cdsl_rule_t* metric_rule = cdsl_parse_string(metric_dsl, NULL);
	if (!simple_rule || !metric_rule) {
		fprintf(stderr, "Failed to parse benchmark rules\n");
		return 1;
	}

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_int(ctx, "user.age", 25);
	cdsl_context_set_int(ctx, "user.score", 70);

	/* ---- Benchmark 1: Parse simple rule ---- */
	{
		double start = get_time_us();
		for (int i = 0; i < BENCH_ITERATIONS; i++) {
			cdsl_rule_t* r = cdsl_parse_string(simple_dsl, NULL);
			cdsl_free_rule(r);
		}
		double elapsed = get_time_us() - start;
		report("Parse simple rule", elapsed, BENCH_ITERATIONS);
	}

	/* ---- Benchmark 2: Execute simple rule (tree-walk) ---- */
	{
		double start = get_time_us();
		for (int i = 0; i < BENCH_ITERATIONS; i++) {
			cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, simple_rule, ctx);
			cdsl_report_free(rpt);
		}
		double elapsed = get_time_us() - start;
		report("Execute simple rule (tree-walk)", elapsed, BENCH_ITERATIONS);
	}

	/* ---- Benchmark 3: Execute metric rule (tree-walk) ---- */
	{
		double start = get_time_us();
		for (int i = 0; i < BENCH_ITERATIONS; i++) {
			cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, metric_rule, ctx);
			cdsl_report_free(rpt);
		}
		double elapsed = get_time_us() - start;
		report("Execute metric rule (tree-walk)", elapsed, BENCH_ITERATIONS);
	}

	/* ---- Benchmark 4: Bytecode compile ---- */
	{
		cdsl_bytecode_t bc = {0};
		double start = get_time_us();
		for (int i = 0; i < BENCH_ITERATIONS; i++) {
			cdsl_bytecode_compile(simple_rule, schema, &bc);
			cdsl_bytecode_free(&bc);
			memset(&bc, 0, sizeof(bc));
		}
		double elapsed = get_time_us() - start;
		report("Bytecode compile simple rule", elapsed, BENCH_ITERATIONS);
	}

	/* ---- Benchmark 5: Bytecode execute simple rule ---- */
	{
		cdsl_bytecode_t bc = {0};
		cdsl_bytecode_compile(simple_rule, schema, &bc);
		double start = get_time_us();
		for (int i = 0; i < BENCH_ITERATIONS; i++) {
			cdsl_value_t v = cdsl_bytecode_execute(vm, &bc, ctx);
			(void)v;
		}
		double elapsed = get_time_us() - start;
		report("Execute simple rule (bytecode)", elapsed, BENCH_ITERATIONS);
		cdsl_bytecode_free(&bc);
	}

	/* ---- Benchmark 6: RuleSet batch execution ---- */
	{
		cdsl_ruleset_t* set = cdsl_ruleset_create();
		cdsl_ruleset_add(set, cdsl_parse_string(simple_dsl, NULL), 0);
		cdsl_ruleset_add(set, cdsl_parse_string(metric_dsl, NULL), 0);
		cdsl_ruleset_add(set, cdsl_parse_string(simple_dsl, NULL), 0);
		cdsl_ruleset_add(set, cdsl_parse_string(metric_dsl, NULL), 0);

		double start = get_time_us();
		for (int i = 0; i < 1000; i++) {
			cdsl_ruleset_report_t* rpt = cdsl_vm_execute_ruleset(vm, set, ctx);
			cdsl_ruleset_report_free(rpt);
		}
		double elapsed = get_time_us() - start;
		report("RuleSet batch (4 rules)", elapsed, 1000);
		cdsl_ruleset_free(set);
	}

	/* ---- Benchmark 7: Parallel RuleSet execution ---- */
	{
		cdsl_ruleset_t* set = cdsl_ruleset_create();
		cdsl_ruleset_add(set, cdsl_parse_string(simple_dsl, NULL), 0);
		cdsl_ruleset_add(set, cdsl_parse_string(metric_dsl, NULL), 0);
		cdsl_ruleset_add(set, cdsl_parse_string(simple_dsl, NULL), 0);
		cdsl_ruleset_add(set, cdsl_parse_string(metric_dsl, NULL), 0);

		double start = get_time_us();
		for (int i = 0; i < 1000; i++) {
			cdsl_ruleset_report_t* rpt = cdsl_vm_execute_ruleset_parallel(vm, set, ctx, 4);
			cdsl_ruleset_report_free(rpt);
		}
		double elapsed = get_time_us() - start;
		report("RuleSet parallel (4 rules, 4 threads)", elapsed, 1000);
		cdsl_ruleset_free(set);
	}

	/* ---- Cleanup ---- */
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_free_rule(simple_rule);
	cdsl_free_rule(metric_rule);
	cdsl_schema_free(schema);

	printf("\n========================================\n");
	return 0;
}
