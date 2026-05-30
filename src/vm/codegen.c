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
#include <ctype.h>

static void
sanitize_identifier(char* buf, size_t bufsize, const char* src)
{
	size_t j = 0;
	for (size_t i = 0; src[i] && j + 1 < bufsize; i++) {
		if (isalnum((unsigned char)src[i]) || src[i] == '_') {
			buf[j++] = src[i];
		} else {
			if (j + 1 < bufsize) {
				buf[j++] = '_';
			}
		}
	}
	buf[j] = '\0';
}

/**
 * @brief Recursively emit a C expression from an AST node (internal).
 *
 * @param f Output file stream
 * @param expr Expression node to translate
 * @param indent Current indentation string
 */
static void
codegen_expr(FILE* f, cdsl_expr_node_t* expr, const cdsl_schema_t* schema, const char* indent)
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
	case CDSL_EXPR_DATE:
		fprintf(f, "%ldL /* %s */", (long)expr->data.date_val, "date literal");
		break;
	case CDSL_EXPR_ID: {
		cdsl_type_t t = CDSL_TYPE_INT;
		if (schema) {
			for (cdsl_var_schema_t* v = schema->vars; v; v = v->next) {
				if (strcmp(v->name, expr->data.id_val) == 0) {
					t = v->type;
					break;
				}
			}
		}
		const char* getter = "get_int";
		if (t == CDSL_TYPE_FLOAT) {
			getter = "get_float";
		} else if (t == CDSL_TYPE_BOOL) {
			getter = "get_bool";
		} else if (t == CDSL_TYPE_STRING) {
			getter = "get_string";
		}
		fprintf(f, "%s(ctx, \"%s\")", getter, expr->data.id_val);
		break;
	}
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
		case CDSL_OP_ADD:
			op_str = "+";
			break;
		case CDSL_OP_SUB:
			op_str = "-";
			break;
		case CDSL_OP_MUL:
			op_str = "*";
			break;
		case CDSL_OP_DIV:
			op_str = "/";
			break;
		default:
			break;
		}
		fprintf(f, "(");
		codegen_expr(f, expr->data.binary.left, schema, indent);
		fprintf(f, " %s ", op_str);
		codegen_expr(f, expr->data.binary.right, schema, indent);
		fprintf(f, ")");
		break;
	}
	case CDSL_EXPR_UNARY:
		fprintf(f, expr->data.unary.op == CDSL_OP_NOT ? "!" : "-");
		codegen_expr(f, expr->data.unary.expr, schema, indent);
		break;
	case CDSL_EXPR_CALL:
		fprintf(f, "func_%s(ctx", expr->data.call.func_name);
		for (cdsl_arg_node_t* a = expr->data.call.args; a; a = a->next) {
			fprintf(f, ", ");
			codegen_expr(f, a->expr, schema, indent);
		}
		fprintf(f, ")");
		break;
	}
}

/**
 * @brief Emit a C statement for an action node (internal).
 */
static void
codegen_action(FILE* f, cdsl_action_node_t* action, const cdsl_schema_t* schema, const char* indent)
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
		codegen_expr(f, a->expr, schema, indent);
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
				codegen_expr(f, c->condition, schema, "        ");
				fprintf(f, ") {\n");
				if (c->action) {
					if (strcmp(c->action->action_name, "score") == 0 &&
					    c->action->args) {
						fprintf(f, "        total_score += ");
						codegen_expr(f,
							     c->action->args->expr,
							     schema,
							     "            ");
						fprintf(f, ";\n");
					} else {
						codegen_action(f, c->action, schema, "        ");
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
					codegen_action(f, m->default_action, schema, "    ");
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
		codegen_expr(f, rule->when_expr, schema, "        ");
		fprintf(f, ") {\n");
		codegen_action(f, rule->then_action, schema, "        ");
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

char*
cdsl_codegen_ruleset_to_h(const cdsl_ruleset_t* set,
			  const cdsl_schema_t* schema,
			  const char* module_name)
{
	(void)schema;
	if (!set || !module_name) {
		return NULL;
	}
	char* buf = NULL;
	size_t len = 0;
	FILE* f = open_memstream(&buf, &len);
	if (!f) {
		return NULL;
	}

	char guard[512];
	char safe_name[256];
	sanitize_identifier(safe_name, sizeof(safe_name), module_name);
	snprintf(guard, sizeof(guard), "CDSL_GENERATED_%s_H", safe_name);
	for (int i = 0; guard[i]; i++) {
		if (guard[i] >= 'a' && guard[i] <= 'z') {
			guard[i] -= 32;
		}
	}

	fprintf(f, "#ifndef %s\n#define %s\n\n", guard, guard);
	fprintf(f, "/* Auto-generated header for ruleset module: %s */\n\n", module_name);

	/* Prototypes for rules */
	for (cdsl_ruleset_entry_t* e = set->entries; e; e = e->next) {
		if (e->rule->metrics) {
			fprintf(
			    f,
			    "int cdsl_eval_rule_%s(int (*get_int)(void* ctx, const char* name),\n",
			    e->rule->name);
			fprintf(
			    f,
			    "                 void (*action)(const char* name, int score, const "
			    "char* reason, void* ud),\n");
			fprintf(f, "                 void* ctx, void* ud);\n\n");
		} else {
			fprintf(
			    f,
			    "int cdsl_eval_rule_%s(int (*get_int)(void* ctx, const char* name),\n",
			    e->rule->name);
			fprintf(f,
				"                 void (*action)(const char* name, void* ud),\n");
			fprintf(f, "                 void* ctx, void* ud);\n\n");
		}
	}

	/* Master ruleset prototype */
	fprintf(f,
		"int cdsl_eval_ruleset_%s(int (*get_int)(void* ctx, const char* name),\n",
		module_name);
	fprintf(f, "                     void* ctx, void* ud);\n\n");

	fprintf(f, "#endif /* %s */\n", guard);

	fflush(f);
	fclose(f);
	return buf;
}

char*
cdsl_codegen_ruleset_to_c(const cdsl_ruleset_t* set,
			  const cdsl_schema_t* schema,
			  const char* module_name)
{
	if (!set || !module_name) {
		return NULL;
	}
	char* buf = NULL;
	size_t len = 0;
	FILE* f = open_memstream(&buf, &len);
	if (!f) {
		return NULL;
	}

	fprintf(f, "#include \"%s.h\"\n", module_name);
	fprintf(f, "#include <stdio.h>\n#include <string.h>\n\n");

	/* Individual rules */
	for (cdsl_ruleset_entry_t* e = set->entries; e; e = e->next) {
		char* rule_c = cdsl_codegen_rule_to_c(e->rule, schema);
		if (rule_c) {
			/* Skip the header comments and standard includes already in rule_c */
			char* body = strstr(rule_c, "int cdsl_eval_rule_");
			if (body) {
				fprintf(f, "%s\n", body);
			}
			free(rule_c);
		}
	}

	/* Master ruleset evaluation function */
	fprintf(f,
		"int cdsl_eval_ruleset_%s(int (*get_int)(void* ctx, const char* name),\n",
		module_name);
	fprintf(f, "                     void* ctx, void* ud) {\n");
	fprintf(f, "    int total_score = 0;\n");
	fprintf(f, "    int res;\n\n");

	for (cdsl_ruleset_entry_t* e = set->entries; e; e = e->next) {
		fprintf(f, "    /* Rule: %s (priority: %d) */\n", e->rule->name, e->priority);
		if (e->rule->metrics) {
			fprintf(f,
				"    res = cdsl_eval_rule_%s(get_int, NULL, ctx, ud);\n",
				e->rule->name);
			fprintf(f, "    if (res < 0) return -1; /* critical veto */\n");
			fprintf(f, "    total_score += res;\n\n");
		} else {
			fprintf(f,
				"    res = cdsl_eval_rule_%s(get_int, NULL, ctx, ud);\n",
				e->rule->name);
			fprintf(f, "    if (res == 0) return -1; /* simple rule failure */\n\n");
		}
	}

	fprintf(f, "    return total_score;\n");
	fprintf(f, "}\n");

	fflush(f);
	fclose(f);
	return buf;
}

int
cdsl_codegen_ruleset_to_files(const cdsl_ruleset_t* set,
			      const cdsl_schema_t* schema,
			      const char* base_path)
{
	if (!set || !base_path) {
		return 0;
	}

	/* Extract module name from base_path */
	const char* module_name = strrchr(base_path, '/');
	if (module_name) {
		module_name++;
	} else {
		module_name = base_path;
	}

	char* h_content = cdsl_codegen_ruleset_to_h(set, schema, module_name);
	char* c_content = cdsl_codegen_ruleset_to_c(set, schema, module_name);

	if (!h_content || !c_content) {
		free(h_content);
		free(c_content);
		return 0;
	}

	char h_path[512], c_path[512];
	snprintf(h_path, sizeof(h_path), "%s.h", base_path);
	snprintf(c_path, sizeof(c_path), "%s.c", base_path);

	FILE* fh = fopen(h_path, "w");
	if (fh) {
		fputs(h_content, fh);
		fclose(fh);
	}

	FILE* fc = fopen(c_path, "w");
	if (fc) {
		fputs(c_content, fc);
		fclose(fc);
	}

	free(h_content);
	free(c_content);
	return 1;
}
/** @} */
