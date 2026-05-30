/**
 * @file vm_codegen.c
 * @brief DSL-to-C code generation implementation.
 *
 * Translates parsed DSL rule ASTs into compilable C source code.
 * Generates evaluate_*() functions with appropriate signatures for
 * both simple (WHEN/THEN) and metric-based (scoring) rules.
 *
 * The generated C code uses callback function pointers for variable
 * access and action dispatch, making it portable across applications.
 *
 * @defgroup codegen Code Generation
 * @{
 */

#include "cdsl/execution.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/**
 * @brief Recursively emit a C expression from an AST node (internal).
 *
 * @param f Output file stream
 * @param expr Expression node to translate
 * @param indent Current indentation string
 */
static void
codegen_expr(FILE* f, cdsl_expr_node_t* expr, const char* indent)
{
	if (!expr) {
		return;
	}
	switch (expr->type) {
	case CDSL_EXPR_INT:
		fprintf(f, "%d", expr->data.int_val);
		break;
	case CDSL_EXPR_FLOAT:
		fprintf(f, "%.2f", expr->data.float_val);
		break;
	case CDSL_EXPR_BOOL:
		fprintf(f, "%s", expr->data.bool_val ? "1" : "0");
		break;
	case CDSL_EXPR_STRING:
		fprintf(f, "\"%s\"", expr->data.string_val);
		break;
	case CDSL_EXPR_ID:
		fprintf(f, "ctx_get_int(ctx, \"%s\")", expr->data.id_val);
		break;
	case CDSL_EXPR_BINARY: {
		const char* op_str = "?";
		switch (expr->data.binary.op) {
		case CDSL_OP_EQ:
			op_str = "==";
			break;
		case CDSL_OP_NE:
			op_str = "!=";
			break;
		case CDSL_OP_LT:
			op_str = "<";
			break;
		case CDSL_OP_GT:
			op_str = ">";
			break;
		case CDSL_OP_LE:
			op_str = "<=";
			break;
		case CDSL_OP_GE:
			op_str = ">=";
			break;
		case CDSL_OP_AND:
			op_str = "&&";
			break;
		case CDSL_OP_OR:
			op_str = "||";
			break;
		default:
			break;
		}
		fprintf(f, "(");
		codegen_expr(f, expr->data.binary.left, indent);
		fprintf(f, " %s ", op_str);
		codegen_expr(f, expr->data.binary.right, indent);
		fprintf(f, ")");
		break;
	}
	case CDSL_EXPR_UNARY:
		fprintf(f, "!");
		codegen_expr(f, expr->data.unary.expr, indent);
		break;
	case CDSL_EXPR_CALL:
		fprintf(f, "func_%s(ctx", expr->data.call.func_name);
		for (cdsl_arg_node_t* a = expr->data.call.args; a; a = a->next) {
			fprintf(f, ", ");
			codegen_expr(f, a->expr, indent);
		}
		fprintf(f, ")");
		break;
	}
}

/**
 * @brief Emit a C statement for an action node (internal).
 */
static void
codegen_action(FILE* f, cdsl_action_node_t* action, const char* indent)
{
	if (!action) {
		return;
	}
	fprintf(f, "%saction_%s(", indent, action->action_name);
	int first = 1;
	for (cdsl_arg_node_t* a = action->args; a; a = a->next) {
		if (!first) {
			fprintf(f, ", ");
		}
		first = 0;
		codegen_expr(f, a->expr, indent);
	}
	fprintf(f, ");\n");
}

char*
cdsl_codegen_rule_to_c(const cdsl_rule_t* rule, const cdsl_schema_t* schema)
{
	if (!rule || !schema) {
		return NULL;
	}
	char* buf = NULL;
	size_t len = 0;
	FILE* f = open_memstream(&buf, &len);
	if (!f) {
		return NULL;
	}

	const char* rname = rule->name ? rule->name : "unnamed_rule";
	fprintf(f, "/* Auto-generated C code from DSL rule: %s */\n", rname);
	fprintf(f, "#include <stdio.h>\n#include <string.h>\n\n");

	if (rule->metrics) {
		const char* p_thresh = cdsl_meta_get(rule->meta_list, "pass_threshold");
		const char* pa_thresh = cdsl_meta_get(rule->meta_list, "partial_threshold");
		fprintf(f,
			"/* Meta: pass_threshold=%s, partial_threshold=%s */\n",
			p_thresh ? p_thresh : "100",
			pa_thresh ? pa_thresh : "50");

		fprintf(f,
			"int cdsl_eval_rule_%s(int (*get_int)(void* ctx, const char* name),\n",
			rule->name);
		fprintf(f,
			"                 void (*action)(const char* name, int score, const char* "
			"reason, void* ud),\n");
		fprintf(f, "                 void* ctx, void* ud) {\n");
		fprintf(f, "    int total_score = 0;\n");
		for (cdsl_metric_node_t* m = rule->metrics; m; m = m->next) {
			const char* w_meta = cdsl_meta_get(m->meta_list, "weight");
			int weight = atoi(w_meta ? w_meta : "0");
			const char* c_meta = cdsl_meta_get(m->meta_list, "is_critical");
			int critical = (strcmp(c_meta ? c_meta : "false", "true") == 0);
			fprintf(f,
				"    /* Metric: %s (weight=%d%s) */\n",
				m->name,
				weight,
				critical ? ", is_critical=true" : "");
			for (cdsl_case_node_t* c = m->case_list; c; c = c->next) {
				fprintf(f, "    if (");
				codegen_expr(f, c->condition, "        ");
				fprintf(f, ") {\n");
				if (c->action) {
					if (strcmp(c->action->action_name, "score") == 0 &&
					    c->action->args) {
						fprintf(f, "        total_score += ");
						codegen_expr(
						    f, c->action->args->expr, "            ");
						fprintf(f, ";\n");
					} else {
						codegen_action(f, c->action, "        ");
					}
				}
				fprintf(f, "        goto next_metric_%s;\n", m->name);
				fprintf(f, "    }\n");
			}
			fprintf(f, "    /* DEFAULT */\n");
			if (m->default_action) {
				if (strcmp(m->default_action->action_name, "fail_metric") == 0) {
					fprintf(
					    f,
					    "    action(\"fail_metric\", 0, \"default\", ud);\n");
					if (critical) {
						fprintf(f, "    return -1; /* critical veto */\n");
					}
				} else {
					codegen_action(f, m->default_action, "    ");
				}
			}
			fprintf(f, "    next_metric_%s: ;\n", m->name);
		}
		fprintf(f, "    return total_score;\n");
		fprintf(f, "}\n");
	} else {
		fprintf(f,
			"int cdsl_eval_rule_%s(int (*get_int)(void* ctx, const char* name),\n",
			rule->name);
		fprintf(f, "                 void (*action)(const char* name, void* ud),\n");
		fprintf(f, "                 void* ctx, void* ud) {\n");
		fprintf(f, "    if (");
		codegen_expr(f, rule->when_expr, "        ");
		fprintf(f, ") {\n");
		codegen_action(f, rule->then_action, "        ");
		fprintf(f, "        return 0; /* FAILED */\n");
		fprintf(f, "    }\n");
		fprintf(f, "    return 1; /* PASSED */\n");
		fprintf(f, "}\n");
	}

	fflush(f);
	fclose(f);
	return buf;
}

int
cdsl_codegen_to_file(const cdsl_rule_t* rule, const cdsl_schema_t* schema, const char* filepath)
{
	if (!rule || !filepath) {
		return 0;
	}
	char* code = cdsl_codegen_rule_to_c(rule, schema);
	if (!code) {
		return 0;
	}
	FILE* f = fopen(filepath, "w");
	if (!f) {
		free(code);
		return 0;
	}
	fputs(code, f);
	fclose(f);
	free(code);
	return 1;
}
/** @} */
