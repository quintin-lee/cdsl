/**
 * @file vm_codegen.c
 * @brief DSL-to-C code generation implementation.
 */

#include "cdsl/execution.h"
#include "internal.h"
#include "cdsl/util/strbuf.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
#include <ctype.h>

static int
safe_atoi(const char* str, int default_val)
{
	if (!str) return default_val;
	char* end = NULL;
	errno = 0;
	long val = strtol(str, &end, 10);
	if (errno != 0 || end == str || *end != '\0') return default_val;
	return (int)val;
}

static void
sanitize_identifier(char* buf, size_t bufsize, const char* src)
{
	size_t j = 0;
	for (size_t i = 0; src[i] && j + 1 < bufsize; i++) {
		if (isalnum((unsigned char)src[i]) || src[i] == '_') buf[j++] = src[i];
		else if (j + 1 < bufsize) buf[j++] = '_';
	}
	buf[j] = '\0';
}

static void
codegen_expr(cdsl_strbuf_t* sb, cdsl_expr_node_t* expr, const cdsl_schema_t* schema, const char* indent)
{
	if (!expr) return;
	switch (expr->type) {
	case CDSL_EXPR_INT: cdsl_strbuf_printf(sb, "%d", expr->data.int_val); break;
	case CDSL_EXPR_FLOAT: cdsl_strbuf_printf(sb, "%.2f", expr->data.float_val); break;
	case CDSL_EXPR_BOOL: cdsl_strbuf_printf(sb, "%s", expr->data.bool_val ? "1" : "0"); break;
	case CDSL_EXPR_STRING: cdsl_strbuf_printf(sb, "\"%s\"", expr->data.string_val); break;
	case CDSL_EXPR_DATE: cdsl_strbuf_printf(sb, "%ldL", (long)expr->data.date_val); break;
	case CDSL_EXPR_LONG: cdsl_strbuf_printf(sb, "%ldL", (long)expr->data.long_val); break;
	case CDSL_EXPR_ID: {
		cdsl_type_t t = CDSL_TYPE_INT;
		if (schema) {
			for (cdsl_var_schema_t* v = schema->vars; v; v = v->next) {
				if (strcmp(v->name, expr->data.id_val) == 0) { t = v->type; break; }
			}
		}
		const char* getter = (t == CDSL_TYPE_FLOAT) ? "get_float" :
		                     (t == CDSL_TYPE_BOOL) ? "get_bool" :
		                     (t == CDSL_TYPE_STRING) ? "get_string" :
		                     (t == CDSL_TYPE_DATE) ? "get_date" :
		                     (t == CDSL_TYPE_LONG) ? "get_long" : "get_int";
		cdsl_strbuf_printf(sb, "%s(ctx, \"%s\")", getter, expr->data.id_val);
		break;
	}
	case CDSL_EXPR_BINARY:
		cdsl_strbuf_printf(sb, "(");
		codegen_expr(sb, expr->data.binary.left, schema, indent);
		const char* ops[] = { "==", "!=", "<", ">", "<=", ">=", "&&", "||", "+", "-", "*", "/" };
		cdsl_strbuf_printf(sb, " %s ", ops[expr->data.binary.op]);
		codegen_expr(sb, expr->data.binary.right, schema, indent);
		cdsl_strbuf_printf(sb, ")");
		break;
	case CDSL_EXPR_UNARY:
		cdsl_strbuf_printf(sb, expr->data.unary.op == CDSL_OP_NOT ? "!" : "-");
		codegen_expr(sb, expr->data.unary.expr, schema, indent);
		break;
	case CDSL_EXPR_CALL:
		cdsl_strbuf_printf(sb, "func_%s(ctx", expr->data.call.func_name);
		for (cdsl_arg_node_t* a = expr->data.call.args; a; a = a->next) {
			cdsl_strbuf_printf(sb, ", ");
			codegen_expr(sb, a->expr, schema, indent);
		}
		cdsl_strbuf_printf(sb, ")");
		break;
	default: break;
	}
}

static void
codegen_action(cdsl_strbuf_t* sb, cdsl_action_node_t* action, const cdsl_schema_t* schema, const char* indent)
{
	if (!action) return;
	cdsl_strbuf_printf(sb, "%saction_%s(", indent, action->action_name);
	int first = 1;
	for (cdsl_arg_node_t* a = action->args; a; a = a->next) {
		if (!first) cdsl_strbuf_printf(sb, ", ");
		first = 0;
		codegen_expr(sb, a->expr, schema, indent);
	}
	cdsl_strbuf_printf(sb, ");\n");
}

char*
cdsl_codegen_rule_to_c(const cdsl_rule_t* rule, const cdsl_schema_t* schema)
{
	if (!rule || !schema) return NULL;
	cdsl_strbuf_t sb;
	cdsl_strbuf_init(&sb, 4096);
	const char* rname = rule->name ? rule->name : "unnamed_rule";
	cdsl_strbuf_printf(&sb, "/* Auto-generated C code from DSL rule: %s */\n", rname);
	cdsl_strbuf_printf(&sb, "#include <stdio.h>\n#include <string.h>\n\n");
	if (rule->metrics) {
		const char* p_thresh = cdsl_meta_get(rule->meta_list, "pass_threshold");
		const char* pa_thresh = cdsl_meta_get(rule->meta_list, "partial_threshold");
		cdsl_strbuf_printf(&sb, "/* Meta: pass_threshold=%s, partial_threshold=%s */\n", p_thresh ? p_thresh : "100", pa_thresh ? pa_thresh : "50");
		cdsl_strbuf_printf(&sb, "int cdsl_eval_rule_%s(int (*get_int)(void* ctx, const char* name), void (*action)(const char* name, int score, const char* reason, void* ud), void* ctx, void* ud) {\n", rule->name);
		cdsl_strbuf_printf(&sb, "    int total_score = 0;\n");
		for (cdsl_metric_node_t* m = rule->metrics; m; m = m->next) {
			const char* w_meta = cdsl_meta_get(m->meta_list, "weight");
			int weight = safe_atoi(w_meta, 0);
			const char* c_meta = cdsl_meta_get(m->meta_list, "is_critical");
			int critical = (strcmp(c_meta ? c_meta : "false", "true") == 0);
			cdsl_strbuf_printf(&sb, "    /* Metric: %s (weight=%d%s) */\n", m->name, weight, critical ? ", is_critical=true" : "");
			for (cdsl_case_node_t* c = m->case_list; c; c = c->next) {
				cdsl_strbuf_printf(&sb, "    if ("); codegen_expr(&sb, c->condition, schema, "        "); cdsl_strbuf_printf(&sb, ") {\n");
				if (c->action) {
					if (strcmp(c->action->action_name, "score") == 0 && c->action->args) {
						cdsl_strbuf_printf(&sb, "        total_score += "); codegen_expr(&sb, c->action->args->expr, schema, "            "); cdsl_strbuf_printf(&sb, ";\n");
					} else codegen_action(&sb, c->action, schema, "        ");
				}
				cdsl_strbuf_printf(&sb, "        goto next_metric_%s;\n    }\n", m->name);
			}
			if (m->default_action) {
				if (strcmp(m->default_action->action_name, "fail_metric") == 0) {
					cdsl_strbuf_printf(&sb, "    action(\"fail_metric\", 0, \"default\", ud);\n");
					if (critical) cdsl_strbuf_printf(&sb, "    return -1;\n");
				} else codegen_action(&sb, m->default_action, schema, "    ");
			}
			cdsl_strbuf_printf(&sb, "    next_metric_%s: ;\n", m->name);
		}
		cdsl_strbuf_printf(&sb, "    return total_score;\n}\n");
	} else {
		cdsl_strbuf_printf(&sb, "int cdsl_eval_rule_%s(int (*get_int)(void* ctx, const char* name), void (*action)(const char* name, void* ud), void* ctx, void* ud) {\n", rule->name);
		cdsl_strbuf_printf(&sb, "    if ("); codegen_expr(&sb, rule->when_expr, schema, "        "); cdsl_strbuf_printf(&sb, ") {\n");
		codegen_action(&sb, rule->then_action, schema, "        ");
		cdsl_strbuf_printf(&sb, "        return 0;\n    }\n    return 1;\n}\n");
	}
	char* result = sb.buf;
	return result;
}

int cdsl_codegen_to_file(const cdsl_rule_t* rule, const cdsl_schema_t* schema, const char* filepath) {
	char* code = cdsl_codegen_rule_to_c(rule, schema);
	if (!code) return 0;
	FILE* f = fopen(filepath, "w");
	if (!f) { free(code); return 0; }
	fputs(code, f); fclose(f); free(code);
	return 1;
}

char* cdsl_codegen_ruleset_to_h(const cdsl_ruleset_t* set, const cdsl_schema_t* schema, const char* module_name) {
	(void)schema; if (!set || !module_name) return NULL;
	cdsl_strbuf_t sb; cdsl_strbuf_init(&sb, 4096);
	char guard[512], safe_name[256];
	sanitize_identifier(safe_name, sizeof(safe_name), module_name);
	snprintf(guard, sizeof(guard), "CDSL_GENERATED_%s_H", safe_name);
	for (int i = 0; guard[i]; i++) if (guard[i] >= 'a' && guard[i] <= 'z') guard[i] -= 32;
	cdsl_strbuf_printf(&sb, "#ifndef %s\n#define %s\n\n", guard, guard);
	for (cdsl_ruleset_entry_t* e = set->entries; e; e = e->next) {
		if (e->rule->metrics) cdsl_strbuf_printf(&sb, "int cdsl_eval_rule_%s(int (*get_int)(void* ctx, const char* name), void (*action)(const char* name, int score, const char* reason, void* ud), void* ctx, void* ud);\n\n", e->rule->name);
		else cdsl_strbuf_printf(&sb, "int cdsl_eval_rule_%s(int (*get_int)(void* ctx, const char* name), void (*action)(const char* name, void* ud), void* ctx, void* ud);\n\n", e->rule->name);
	}
	cdsl_strbuf_printf(&sb, "int cdsl_eval_ruleset_%s(int (*get_int)(void* ctx, const char* name), void* ctx, void* ud);\n\n#endif\n", module_name);
	return sb.buf;
}

char* cdsl_codegen_ruleset_to_c(const cdsl_ruleset_t* set, const cdsl_schema_t* schema, const char* module_name) {
	if (!set || !module_name) return NULL;
	cdsl_strbuf_t sb; cdsl_strbuf_init(&sb, 8192);
	cdsl_strbuf_printf(&sb, "#include \"%s.h\"\n#include <stdio.h>\n#include <string.h>\n\n", module_name);
	for (cdsl_ruleset_entry_t* e = set->entries; e; e = e->next) {
		char* rule_c = cdsl_codegen_rule_to_c(e->rule, schema);
		if (rule_c) {
			char* body = strstr(rule_c, "int cdsl_eval_rule_");
			if (body) cdsl_strbuf_printf(&sb, "%s\n", body);
			free(rule_c);
		}
	}
	cdsl_strbuf_printf(&sb, "int cdsl_eval_ruleset_%s(int (*get_int)(void* ctx, const char* name), void* ctx, void* ud) {\n    int total_score = 0, res;\n", module_name);
	for (cdsl_ruleset_entry_t* e = set->entries; e; e = e->next) {
		if (e->rule->metrics) {
			cdsl_strbuf_printf(&sb, "    res = cdsl_eval_rule_%s(get_int, NULL, ctx, ud);\n    if (res < 0) return -1;\n    total_score += res;\n", e->rule->name);
		} else {
			cdsl_strbuf_printf(&sb, "    res = cdsl_eval_rule_%s(get_int, NULL, ctx, ud);\n    if (res == 0) return -1;\n", e->rule->name);
		}
	}
	cdsl_strbuf_printf(&sb, "    return total_score;\n}\n");
	return sb.buf;
}

int cdsl_codegen_ruleset_to_files(const cdsl_ruleset_t* set, const cdsl_schema_t* schema, const char* base_path) {
	if (!set || !base_path) return 0;
	const char* module_name = strrchr(base_path, '/');
	module_name = module_name ? module_name + 1 : base_path;
	char* h = cdsl_codegen_ruleset_to_h(set, schema, module_name);
	char* c = cdsl_codegen_ruleset_to_c(set, schema, module_name);
	if (!h || !c) { free(h); free(c); return 0; }
	char h_path[512], c_path[512];
	snprintf(h_path, sizeof(h_path), "%s.h", base_path);
	snprintf(c_path, sizeof(c_path), "%s.c", base_path);
	FILE* fh = fopen(h_path, "w"); if (fh) { fputs(h, fh); fclose(fh); }
	FILE* fc = fopen(c_path, "w"); if (fc) { fputs(c, fc); fclose(fc); }
	free(h); free(c); return 1;
}
