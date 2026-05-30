/**
 * @file src/ast/template.c
 * @brief Template registry and EXTENDS rule implementation.
 *
 * @ingroup cdsl_ast
 * @defgroup cdsl_template Template registry implementation
 * @{
 */

#include "cdsl/ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- Deep-copy helpers (internal) ---- */

static cdsl_arg_node_t* copy_arg_list(const cdsl_arg_node_t* list);

static cdsl_expr_node_t*
copy_expr(const cdsl_expr_node_t* expr)
{
	if (!expr) {
		return NULL;
	}
	cdsl_expr_node_t* c = calloc(1, sizeof(*c));
	c->type = expr->type;
	switch (expr->type) {
	case CDSL_EXPR_INT:
		c->data.int_val = expr->data.int_val;
		break;
	case CDSL_EXPR_FLOAT:
		c->data.float_val = expr->data.float_val;
		break;
	case CDSL_EXPR_BOOL:
		c->data.bool_val = expr->data.bool_val;
		break;
	case CDSL_EXPR_ID:
		c->data.id_val = strdup(expr->data.id_val);
		break;
	case CDSL_EXPR_STRING:
		c->data.string_val = strdup(expr->data.string_val);
		break;
	case CDSL_EXPR_UNARY:
		c->data.unary.op = expr->data.unary.op;
		c->data.unary.expr = copy_expr(expr->data.unary.expr);
		break;
	case CDSL_EXPR_BINARY:
		c->data.binary.op = expr->data.binary.op;
		c->data.binary.left = copy_expr(expr->data.binary.left);
		c->data.binary.right = copy_expr(expr->data.binary.right);
		break;
	case CDSL_EXPR_CALL:
		c->data.call.func_name = strdup(expr->data.call.func_name);
		c->data.call.args = copy_arg_list(expr->data.call.args);
		break;
	}
	return c;
}

static cdsl_arg_node_t*
copy_arg_list(const cdsl_arg_node_t* list)
{
	cdsl_arg_node_t* head = NULL;
	cdsl_arg_node_t* tail = NULL;
	for (const cdsl_arg_node_t* cur = list; cur; cur = cur->next) {
		cdsl_arg_node_t* n = calloc(1, sizeof(*n));
		n->expr = copy_expr(cur->expr);
		if (!head) {
			head = n;
			tail = n;
		} else {
			tail->next = n;
			tail = n;
		}
	}
	return head;
}

static cdsl_action_node_t*
copy_action(const cdsl_action_node_t* action)
{
	if (!action) {
		return NULL;
	}
	cdsl_action_node_t* c = calloc(1, sizeof(*c));
	c->action_name = strdup(action->action_name);
	c->args = copy_arg_list(action->args);
	return c;
}

static cdsl_metric_node_t*
copy_metric_list(cdsl_metric_node_t* src)
{
	if (!src) {
		return NULL;
	}
	cdsl_metric_node_t* head = NULL;
	cdsl_metric_node_t* tail = NULL;
	for (cdsl_metric_node_t* m = src; m; m = m->next) {
		cdsl_metric_node_t* nm = calloc(1, sizeof(*nm));
		nm->name = strdup(m->name);
		nm->meta_list = NULL;
		for (cdsl_meta_item_t* mi = m->meta_list; mi; mi = mi->next) {
			nm->meta_list = cdsl_append_meta(
			    nm->meta_list,
			    cdsl_create_meta_item(strdup(mi->key), strdup(mi->value)));
		}
		nm->case_list = NULL;
		for (cdsl_case_node_t* c = m->case_list; c; c = c->next) {
			nm->case_list = cdsl_append_case(
			    nm->case_list,
			    cdsl_create_case(copy_expr(c->condition), copy_action(c->action)));
		}
		nm->default_action = copy_action(m->default_action);
		nm->next = NULL;
		if (!head) {
			head = nm;
			tail = nm;
		} else {
			tail->next = nm;
			tail = nm;
		}
	}
	return head;
}

/* ---- Template registry ---- */

typedef struct cdsl_template_entry {
	char* name;
	cdsl_rule_t* rule;
	struct cdsl_template_entry* next;
} cdsl_template_entry_t;

static cdsl_template_entry_t* template_registry = NULL;

void
cdsl_template_register(cdsl_rule_t* template_rule)
{
	if (!template_rule || !template_rule->name) {
		return;
	}
	cdsl_template_entry_t* e = calloc(1, sizeof(*e));
	e->name = strdup(template_rule->name);
	e->rule = template_rule;
	e->next = template_registry;
	template_registry = e;
}

cdsl_rule_t*
cdsl_template_get(const char* name)
{
	for (cdsl_template_entry_t* e = template_registry; e; e = e->next) {
		if (strcmp(e->name, name) == 0) {
			return e->rule;
		}
	}
	return NULL;
}

void
cdsl_template_clear(void)
{
	cdsl_template_entry_t* e = template_registry;
	while (e) {
		cdsl_template_entry_t* next = e->next;
		free(e->name);
		free(e);
		e = next;
	}
	template_registry = NULL;
}

cdsl_rule_t*
cdsl_create_extends_rule(char* name, char* template_name, cdsl_meta_item_t* meta)
{
	cdsl_rule_t* tpl = cdsl_template_get(template_name);
	if (!tpl) {
		fprintf(stderr, "Template '%s' not found\n", template_name);
		free(name);
		free(template_name);
		cdsl_free_meta(meta);
		return NULL;
	}
	cdsl_rule_t* rule = calloc(1, sizeof(*rule));
	rule->name = name;
	rule->meta_list = meta;
	rule->metrics = copy_metric_list(tpl->metrics);
	free(template_name);
	return rule;
}
/** @} */
