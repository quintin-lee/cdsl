/**
 * @file tests/fuzz_parse.c
 * @brief Fuzz target for the DSL parser.
 *
 * Uses libFuzzer (built into Clang) to stress-test cdsl_parse_string()
 * with arbitrary input. Detects crashes, hangs, and memory issues.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "cdsl/ast.h"
#include "cdsl/bytecode.h"
#include "cdsl/cdsl.h"
#include "cdsl/context.h"
#include "cdsl/execution.h"
#include "cdsl/schema.h"

static int
has_error_expr(const cdsl_expr_node_t* expr)
{
	if (!expr) {
		return 0;
	}
	if (expr->type == CDSL_EXPR_ERROR) {
		return 1;
	}
	switch (expr->type) {
	case CDSL_EXPR_UNARY:
		return has_error_expr(expr->data.unary.expr);
	case CDSL_EXPR_BINARY:
		return has_error_expr(expr->data.binary.left) ||
		       has_error_expr(expr->data.binary.right);
	case CDSL_EXPR_CALL:
		for (cdsl_arg_node_t* a = expr->data.call.args; a; a = a->next) {
			if (has_error_expr(a->expr)) {
				return 1;
			}
		}
		return 0;
	case CDSL_EXPR_ARRAY:
		for (cdsl_arg_node_t* a = expr->data.array.elements; a; a = a->next) {
			if (has_error_expr(a->expr)) {
				return 1;
			}
		}
		return 0;
	default:
		return 0;
	}
}

static int
rule_has_error_expr(const cdsl_rule_t* rule)
{
	if (!rule) {
		return 0;
	}
	if (rule->when_expr && has_error_expr(rule->when_expr)) {
		return 1;
	}
	for (cdsl_metric_node_t* m = rule->metrics; m; m = m->next) {
		for (cdsl_case_node_t* c = m->case_list; c; c = c->next) {
			if (has_error_expr(c->condition)) {
				return 1;
			}
		}
	}
	return 0;
}

/**
 * @brief libFuzzer entry point for parser and differential execution fuzzing.
 */
int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	/* Copy to a null-terminated buffer */
	char* dsl = malloc(size + 1);
	if (!dsl) {
		return 0;
	}
	memcpy(dsl, data, size);
	dsl[size] = '\0';

	cdsl_error_list_t* errs = NULL;
	cdsl_rule_t* rule = cdsl_parse_string(dsl, &errs);
	int had_parse_errors = (errs && errs->count > 0);
	if (errs) {
		cdsl_error_list_free(errs);
	}

	if (rule) {
		if (!had_parse_errors && !rule_has_error_expr(rule)) {
			cdsl_schema_t* schema = cdsl_schema_create();
			cdsl_bytecode_t bc = {0};
			if (cdsl_bytecode_compile(rule, schema, &bc)) {
				cdsl_vm_t* vm_ast = cdsl_vm_create(schema);
				cdsl_vm_t* vm_bc = cdsl_vm_create(schema);
				cdsl_context_t* ctx = cdsl_context_create(schema);

				/* Keep execution bounds tight to prevent timeouts */
				cdsl_vm_set_instruction_limit(vm_ast, 500);
				cdsl_vm_set_instruction_limit(vm_bc, 500);
				cdsl_vm_set_memory_limit(vm_ast, 512 * 1024);
				cdsl_vm_set_memory_limit(vm_bc, 512 * 1024);

				cdsl_rule_report_t* rpt_ast = cdsl_vm_execute(vm_ast, rule, ctx);
				cdsl_rule_report_t* rpt_bc =
				    cdsl_bytecode_execute_rule(vm_bc, &bc, ctx);

				if (rpt_ast && rpt_bc && rpt_ast->status != rpt_bc->status) {
					abort();
				}

				if (rpt_ast) {
					cdsl_report_free(rpt_ast);
				}
				if (rpt_bc) {
					cdsl_report_free(rpt_bc);
				}
				cdsl_context_free(ctx);
				cdsl_vm_free(vm_ast);
				cdsl_vm_free(vm_bc);
				cdsl_bytecode_free(&bc);
			}
			cdsl_schema_free(schema);
		}
		cdsl_free_rule(rule);
	}
	free(dsl);
	return 0;
}
