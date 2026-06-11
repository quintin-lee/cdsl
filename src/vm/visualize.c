/**
 * @file vm_visualize.c
 * @brief Graphviz DOT graph generation for rule visualization.
 */

#include "cdsl/execution.h"
#include "internal.h"
#include "cdsl/util/strbuf.h"
#include "cdsl/util/portability.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

static int
safe_atoi(const char* str, int default_val)
{
	if (!str) {
		return default_val;
	}
	char* end = NULL;
	errno = 0;
	long val = strtol(str, &end, 10);
	if (errno != 0 || end == str || *end != '\0') {
		return default_val;
	}
	return (int)val;
}

static THREAD_LOCAL int dot_id = 0;

static void
dot_expr(cdsl_strbuf_t* sb, cdsl_expr_node_t* expr, int* id)
{
	if (!expr) {
		return;
	}
	int my_id = (*id)++;
	switch (expr->type) {
	case CDSL_EXPR_INT:
		cdsl_strbuf_printf(
		    sb,
		    "  n%d [label=\"%d\",shape=box,style=filled,fillcolor=lightyellow];\n",
		    my_id,
		    expr->data.int_val);
		break;
	case CDSL_EXPR_FLOAT:
		cdsl_strbuf_printf(
		    sb,
		    "  n%d [label=\"%.2f\",shape=box,style=filled,fillcolor=lightyellow];\n",
		    my_id,
		    expr->data.float_val);
		break;
	case CDSL_EXPR_BOOL:
		cdsl_strbuf_printf(
		    sb,
		    "  n%d [label=\"%s\",shape=box,style=filled,fillcolor=lightyellow];\n",
		    my_id,
		    expr->data.bool_val ? "true" : "false");
		break;
	case CDSL_EXPR_STRING:
		cdsl_strbuf_printf(
		    sb,
		    "  n%d [label=\"\\\"%s\\\"\",shape=box,style=filled,fillcolor=lightyellow];\n",
		    my_id,
		    expr->data.string_val);
		break;
	case CDSL_EXPR_DATE: {
		char b[32];
		struct tm tmb;
		CDSL_LOCALTIME_R(&expr->data.date_val, &tmb);
		strftime(b, sizeof(b), "%Y-%m-%d", &tmb);
		cdsl_strbuf_printf(
		    sb,
		    "  n%d [label=\"@%s\",shape=box,style=filled,fillcolor=lightyellow];\n",
		    my_id,
		    b);
		break;
	}
	case CDSL_EXPR_LONG:
		cdsl_strbuf_printf(
		    sb,
		    "  n%d [label=\"%ldL\",shape=box,style=filled,fillcolor=lightyellow];\n",
		    my_id,
		    (long)expr->data.long_val);
		break;
	case CDSL_EXPR_ARRAY:
		cdsl_strbuf_printf(
		    sb,
		    "  n%d [label=\"[]\",shape=diamond,style=filled,fillcolor=lightgreen];\n",
		    my_id);
		for (cdsl_arg_node_t* a = expr->data.array.elements; a; a = a->next) {
			int cid = *id;
			dot_expr(sb, a->expr, id);
			cdsl_strbuf_printf(sb, "  n%d -> n%d;\n", my_id, cid);
		}
		break;
	case CDSL_EXPR_ID:
		cdsl_strbuf_printf(
		    sb,
		    "  n%d [label=\"%s\",shape=ellipse,style=filled,fillcolor=lightblue];\n",
		    my_id,
		    expr->data.id_val);
		break;
	case CDSL_EXPR_BINARY: {
		const char* ops[] = {
		    "==", "!=", "<", ">", "<=", ">=", "AND", "OR", "+", "-", "*", "/"};
		cdsl_strbuf_printf(
		    sb,
		    "  n%d [label=\"%s\",shape=diamond,style=filled,fillcolor=lightgreen];\n",
		    my_id,
		    ops[expr->data.binary.op]);
		int lid = *id;
		dot_expr(sb, expr->data.binary.left, id);
		cdsl_strbuf_printf(sb, "  n%d -> n%d;\n", my_id, lid);
		int rid = *id;
		dot_expr(sb, expr->data.binary.right, id);
		cdsl_strbuf_printf(sb, "  n%d -> n%d;\n", my_id, rid);
		break;
	}
	case CDSL_EXPR_UNARY:
		cdsl_strbuf_printf(
		    sb,
		    "  n%d [label=\"NOT\",shape=diamond,style=filled,fillcolor=lightgreen];\n",
		    my_id);
		int cid = *id;
		dot_expr(sb, expr->data.unary.expr, id);
		cdsl_strbuf_printf(sb, "  n%d -> n%d;\n", my_id, cid);
		break;
	case CDSL_EXPR_CALL:
		cdsl_strbuf_printf(
		    sb,
		    "  n%d [label=\"%s()\",shape=box,style=filled,fillcolor=lightpink];\n",
		    my_id,
		    expr->data.call.func_name);
		break;
	}
}

char*
cdsl_rule_to_dot(const cdsl_rule_t* rule)
{
	if (!rule) {
		return NULL;
	}
	cdsl_strbuf_t sb;
	cdsl_strbuf_init(&sb, 4096);
	dot_id = 0;
	const char* rname = rule->name ? rule->name : "anonymous";
	cdsl_strbuf_printf(
	    &sb, "digraph rule_%s {\n  rankdir=TB;\n  node [fontname=\"Helvetica\"];\n\n", rname);
	if (rule->metrics) {
		cdsl_strbuf_printf(&sb,
				   "  rule_%s [label=\"%s\\n(Metric "
				   "Rule)\",shape=box,style=filled,fillcolor=gray];\n\n",
				   rname,
				   rname);
		for (cdsl_metric_node_t* m = rule->metrics; m; m = m->next) {
			int weight = safe_atoi(cdsl_meta_get(m->meta_list, "weight"), 0);
			int critical = (strcmp(cdsl_meta_get(m->meta_list, "is_critical")
						   ? cdsl_meta_get(m->meta_list, "is_critical")
						   : "false",
					       "true") == 0);
			cdsl_strbuf_printf(
			    &sb,
			    "  metric_%s "
			    "[label=\"%s\\n(weight=%d%s)\",shape=box,style=filled,fillcolor=%s];\n",
			    m->name,
			    m->name,
			    weight,
			    critical ? ",critical" : "",
			    critical ? "salmon" : "lightcyan");
			cdsl_strbuf_printf(&sb, "  rule_%s -> metric_%s;\n\n", rname, m->name);
			int cnum = 0;
			for (cdsl_case_node_t* c = m->case_list; c; c = c->next) {
				cdsl_strbuf_printf(
				    &sb,
				    "  case_%s_%d [label=\"CASE "
				    "%d\",shape=diamond,style=filled,fillcolor=lightgreen];\n",
				    m->name,
				    cnum,
				    cnum + 1);
				cdsl_strbuf_printf(
				    &sb, "  metric_%s -> case_%s_%d;\n", m->name, m->name, cnum);
				int eid = dot_id++;
				dot_expr(&sb, c->condition, &eid);
				cdsl_strbuf_printf(
				    &sb, "  case_%s_%d -> n%d;\n", m->name, cnum, eid - 1);
				cnum++;
			}
			cdsl_strbuf_printf(
			    &sb,
			    "  default_%s "
			    "[label=\"DEFAULT\",shape=box,style=filled,fillcolor=lightgray];\n",
			    m->name);
			cdsl_strbuf_printf(&sb, "  metric_%s -> default_%s;\n\n", m->name, m->name);
		}
	} else {
		cdsl_strbuf_printf(&sb,
				   "  rule_%s [label=\"%s\\n(Simple "
				   "Rule)\",shape=box,style=filled,fillcolor=gray];\n\n",
				   rname,
				   rname);
		cdsl_strbuf_printf(
		    &sb,
		    "  when_%s [label=\"WHEN\",shape=diamond,style=filled,fillcolor=lightgreen];\n",
		    rname);
		cdsl_strbuf_printf(&sb, "  rule_%s -> when_%s;\n", rname, rname);
		int eid = dot_id++;
		dot_expr(&sb, rule->when_expr, &eid);
		cdsl_strbuf_printf(&sb, "  when_%s -> n%d;\n", rname, eid - 1);
		if (rule->then_action) {
			cdsl_strbuf_printf(&sb,
					   "  then_%s [label=\"THEN: "
					   "%s()\",shape=box,style=filled,fillcolor=lightpink];\n",
					   rname,
					   rule->then_action->action_name);
			cdsl_strbuf_printf(&sb, "  rule_%s -> then_%s;\n", rname, rname);
		}
		cdsl_strbuf_printf(
		    &sb,
		    "  pass_%s [label=\"PASSED\",shape=box,style=filled,fillcolor=lightgreen];\n",
		    rname);
		cdsl_strbuf_printf(
		    &sb,
		    "  fail_%s [label=\"FAILED\",shape=box,style=filled,fillcolor=salmon];\n",
		    rname);
		cdsl_strbuf_printf(&sb, "  when_%s -> pass_%s [label=\"false\"];\n", rname, rname);
		cdsl_strbuf_printf(&sb, "  when_%s -> fail_%s [label=\"true\"];\n", rname, rname);
	}
	cdsl_strbuf_printf(&sb, "}\n");
	return sb.buf;
}

int
cdsl_rule_to_dot_file(const cdsl_rule_t* rule, const char* filepath)
{
	char* dot = cdsl_rule_to_dot(rule);
	if (!dot) {
		return 0;
	}
	FILE* f = fopen(filepath, "w");
	if (!f) {
		free(dot);
		return 0;
	}
	fputs(dot, f);
	fclose(f);
	free(dot);
	return 1;
}

char*
cdsl_ruleset_to_dot(const cdsl_ruleset_t* set)
{
	if (!set) {
		return NULL;
	}
	cdsl_strbuf_t sb;
	cdsl_strbuf_init(&sb, 4096);
	cdsl_strbuf_printf(
	    &sb, "digraph ruleset {\n  rankdir=LR;\n  node [fontname=\"Helvetica\"];\n\n");
	for (cdsl_ruleset_entry_t* e = set->entries; e; e = e->next) {
		if (!e->rule) {
			continue;
		}
		cdsl_strbuf_printf(&sb,
				   "  rule_%s [label=\"%s\\npriority=%d\\n(vars: %s, action: "
				   "%s)\",shape=box,style=filled,fillcolor=lightblue];\n",
				   e->rule->name,
				   e->rule->name,
				   e->priority,
				   e->rule->when_expr ? "present" : "metrics",
				   e->rule->then_action ? e->rule->then_action->action_name
							: "multiple");
		char* deps = cdsl_meta_get(e->rule->meta_list, "depends_on");
		if (deps) {
			char db[1024];
			strncpy(db, deps, sizeof(db) - 1);
			db[sizeof(db) - 1] = '\0';
			char* tok = strtok(db, ",");
			while (tok) {
				while (*tok == ' ') {
					tok++;
				}
				cdsl_strbuf_printf(
				    &sb,
				    "  rule_%s -> rule_%s [style=dashed,color=gray];\n",
				    e->rule->name,
				    tok);
				tok = strtok(NULL, ",");
			}
		}
	}
	cdsl_strbuf_printf(&sb, "}\n");
	return sb.buf;
}

int
cdsl_ruleset_to_dot_file(const cdsl_ruleset_t* set, const char* filepath)
{
	char* dot = cdsl_ruleset_to_dot(set);
	if (!dot) {
		return 0;
	}
	FILE* f = fopen(filepath, "w");
	if (!f) {
		free(dot);
		return 0;
	}
	fputs(dot, f);
	fclose(f);
	free(dot);
	return 1;
}
