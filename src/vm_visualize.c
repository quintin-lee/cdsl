/**
 * @file vm_visualize.c
 * @brief Graphviz DOT graph generation for rule visualization.
 *
 * Generates DOT (Graphviz) representations of:
 * - Individual rule ASTs (expressions, metrics, cases, actions)
 * - RuleSet dependency and priority graphs
 *
 * Visual output distinguishes node types by shape and color:
 * - Expressions: box (literals), ellipse (identifiers), diamond (operators)
 * - Metrics: rounded boxes colored by criticality
 * - RuleSet: LR layout with priority labels and dashed dependency edges
 *
 * @defgroup visualize Graphviz Visualization
 * @{
 */

#include "execution.h"
#include "execution_internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/** @brief Sequential ID counter for DOT graph nodes (internal). */
static int dot_id = 0;

/**
 * @brief Recursively emit a DOT subgraph for an expression (internal).
 */
static void
dot_expr(FILE* f, cdsl_expr_node_t* expr, int* id)
{
	if (!expr) {
		return;
	}
	int my_id = (*id)++;
	switch (expr->type) {
	case CDSL_EXPR_INT:
		fprintf(f,
			"  n%d [label=\"%d\",shape=box,style=filled,fillcolor=lightyellow];\n",
			my_id,
			expr->data.int_val);
		break;
	case CDSL_EXPR_FLOAT:
		fprintf(f,
			"  n%d [label=\"%.2f\",shape=box,style=filled,fillcolor=lightyellow];\n",
			my_id,
			expr->data.float_val);
		break;
	case CDSL_EXPR_BOOL:
		fprintf(f,
			"  n%d [label=\"%s\",shape=box,style=filled,fillcolor=lightyellow];\n",
			my_id,
			expr->data.bool_val ? "true" : "false");
		break;
	case CDSL_EXPR_STRING:
		fprintf(
		    f,
		    "  n%d [label=\"\\\"%s\\\"\",shape=box,style=filled,fillcolor=lightyellow];\n",
		    my_id,
		    expr->data.string_val);
		break;
	case CDSL_EXPR_ID:
		fprintf(f,
			"  n%d [label=\"%s\",shape=ellipse,style=filled,fillcolor=lightblue];\n",
			my_id,
			expr->data.id_val);
		break;
	case CDSL_EXPR_BINARY: {
		const char* op = "?";
		switch (expr->data.binary.op) {
		case CDSL_OP_EQ:
			op = "==";
			break;
		case CDSL_OP_NE:
			op = "!=";
			break;
		case CDSL_OP_LT:
			op = "<";
			break;
		case CDSL_OP_GT:
			op = ">";
			break;
		case CDSL_OP_LE:
			op = "<=";
			break;
		case CDSL_OP_GE:
			op = ">=";
			break;
		case CDSL_OP_AND:
			op = "AND";
			break;
		case CDSL_OP_OR:
			op = "OR";
			break;
		default:
			break;
		}
		fprintf(f,
			"  n%d [label=\"%s\",shape=diamond,style=filled,fillcolor=lightgreen];\n",
			my_id,
			op);
		int left_id = *id;
		dot_expr(f, expr->data.binary.left, id);
		fprintf(f, "  n%d -> n%d;\n", my_id, left_id);
		int right_id = *id;
		dot_expr(f, expr->data.binary.right, id);
		fprintf(f, "  n%d -> n%d;\n", my_id, right_id);
		break;
	}
	case CDSL_EXPR_UNARY:
		fprintf(f,
			"  n%d [label=\"NOT\",shape=diamond,style=filled,fillcolor=lightgreen];\n",
			my_id);
		int child_id = *id;
		dot_expr(f, expr->data.unary.expr, id);
		fprintf(f, "  n%d -> n%d;\n", my_id, child_id);
		break;
	case CDSL_EXPR_CALL:
		fprintf(f,
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
	char* buf = NULL;
	size_t len = 0;
	FILE* f = open_memstream(&buf, &len);
	if (!f) {
		return NULL;
	}

	dot_id = 0;
	fprintf(f, "digraph rule_%s {\n", rule->name);
	fprintf(f, "  rankdir=TB;\n");
	fprintf(f, "  node [fontname=\"Helvetica\"];\n\n");

	if (rule->metrics) {
		fprintf(f,
			"  rule_%s [label=\"%s\\n(Metric "
			"Rule)\",shape=box,style=filled,fillcolor=gray];\n\n",
			rule->name,
			rule->name);
		for (cdsl_metric_node_t* m = rule->metrics; m; m = m->next) {
			const char* w_meta = cdsl_meta_get(m->meta_list, "weight");
			int weight = atoi(w_meta ? w_meta : "0");
			const char* c_meta = cdsl_meta_get(m->meta_list, "is_critical");
			int critical = (strcmp(c_meta ? c_meta : "false", "true") == 0);
			fprintf(
			    f,
			    "  metric_%s "
			    "[label=\"%s\\n(weight=%d%s)\",shape=box,style=filled,fillcolor=%s];\n",
			    m->name,
			    m->name,
			    weight,
			    critical ? ",critical" : "",
			    critical ? "salmon" : "lightcyan");
			fprintf(f, "  rule_%s -> metric_%s;\n\n", rule->name, m->name);

			int case_num = 0;
			for (cdsl_case_node_t* c = m->case_list; c; c = c->next) {
				fprintf(f,
					"  case_%s_%d [label=\"CASE "
					"%d\",shape=diamond,style=filled,fillcolor=lightgreen];\n",
					m->name,
					case_num,
					case_num + 1);
				fprintf(
				    f, "  metric_%s -> case_%s_%d;\n", m->name, m->name, case_num);
				int expr_id = dot_id++;
				dot_expr(f, c->condition, &expr_id);
				fprintf(
				    f, "  case_%s_%d -> n%d;\n", m->name, case_num, expr_id - 1);
				case_num++;
			}
			fprintf(f,
				"  default_%s "
				"[label=\"DEFAULT\",shape=box,style=filled,fillcolor=lightgray];\n",
				m->name);
			fprintf(f, "  metric_%s -> default_%s;\n\n", m->name, m->name);
		}
	} else {
		fprintf(f,
			"  rule_%s [label=\"%s\\n(Simple "
			"Rule)\",shape=box,style=filled,fillcolor=gray];\n\n",
			rule->name,
			rule->name);
		fprintf(
		    f,
		    "  when_%s [label=\"WHEN\",shape=diamond,style=filled,fillcolor=lightgreen];\n",
		    rule->name);
		fprintf(f, "  rule_%s -> when_%s;\n", rule->name, rule->name);
		int expr_id = dot_id++;
		dot_expr(f, rule->when_expr, &expr_id);
		fprintf(f, "  when_%s -> n%d;\n", rule->name, expr_id - 1);
		fprintf(
		    f,
		    "  pass_%s [label=\"PASSED\",shape=box,style=filled,fillcolor=lightgreen];\n",
		    rule->name);
		fprintf(f,
			"  fail_%s [label=\"FAILED\",shape=box,style=filled,fillcolor=salmon];\n",
			rule->name);
		fprintf(f, "  when_%s -> pass_%s [label=\"false\"];\n", rule->name, rule->name);
		fprintf(f, "  when_%s -> fail_%s [label=\"true\"];\n", rule->name, rule->name);
	}

	fprintf(f, "}\n");
	fflush(f);
	fclose(f);
	return buf;
}

int
cdsl_rule_to_dot_file(const cdsl_rule_t* rule, const char* filepath)
{
	if (!rule || !filepath) {
		return 0;
	}
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
	char* buf = NULL;
	size_t len = 0;
	FILE* f = open_memstream(&buf, &len);
	if (!f) {
		return NULL;
	}

	fprintf(f, "digraph ruleset {\n");
	fprintf(f, "  rankdir=LR;\n");
	fprintf(f, "  node [fontname=\"Helvetica\"];\n\n");

	for (cdsl_ruleset_entry_t* e = set->entries; e; e = e->next) {
		if (!e->rule) {
			continue;
		}
		fprintf(
		    f,
		    "  rule_%s "
		    "[label=\"%s\\npriority=%d\",shape=box,style=filled,fillcolor=lightblue];\n",
		    e->rule->name,
		    e->rule->name,
		    e->priority);
		char* deps = cdsl_meta_get(e->rule->meta_list, "depends_on");
		if (deps) {
			char dep_buf[1024];
			strncpy(dep_buf, deps, sizeof(dep_buf) - 1);
			dep_buf[sizeof(dep_buf) - 1] = '\0';
			char* token = strtok(dep_buf, ",");
			while (token) {
				while (*token == ' ') {
					token++;
				}
				fprintf(f,
					"  rule_%s -> rule_%s [style=dashed,color=gray];\n",
					e->rule->name,
					token);
				token = strtok(NULL, ",");
			}
		}
	}

	fprintf(f, "}\n");
	fflush(f);
	fclose(f);
	return buf;
}

int
cdsl_ruleset_to_dot_file(const cdsl_ruleset_t* set, const char* filepath)
{
	if (!set || !filepath) {
		return 0;
	}
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
/** @} */
