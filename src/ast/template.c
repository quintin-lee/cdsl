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
#include "cdsl/util/threads.h"

/* ---- Deep-copy helpers (internal) ---- */

static cdsl_expr_node_t*
copy_expr(const cdsl_expr_node_t* expr)
{
	if (!expr) {
		return NULL;
	}
	switch (expr->type) {
	case CDSL_EXPR_INT:
		return cdsl_create_expr_int(expr->data.int_val);
	case CDSL_EXPR_FLOAT:
		return cdsl_create_expr_float(expr->data.float_val);
	case CDSL_EXPR_BOOL:
		return cdsl_create_expr_bool(expr->data.bool_val);
	case CDSL_EXPR_ID:
		return cdsl_create_expr_id(strdup(expr->data.id_val));
	case CDSL_EXPR_STRING:
		return cdsl_create_expr_string(strdup(expr->data.string_val));
	case CDSL_EXPR_UNARY:
		return cdsl_create_expr_unary(expr->data.unary.op,
					      copy_expr(expr->data.unary.expr));
	case CDSL_EXPR_BINARY:
		return cdsl_create_expr_binary(expr->data.binary.op,
					       copy_expr(expr->data.binary.left),
					       copy_expr(expr->data.binary.right));
	case CDSL_EXPR_CALL: {
		cdsl_arg_node_t* args = NULL;
		for (cdsl_arg_node_t* a = expr->data.call.args; a; a = a->next) {
			args = cdsl_append_arg(args, copy_expr(a->expr));
		}
		return cdsl_create_expr_call(strdup(expr->data.call.func_name), args);
	}
	default:
		return NULL;
	}
}

static cdsl_action_node_t*
copy_action(const cdsl_action_node_t* action)
{
	if (!action) {
		return NULL;
	}
	cdsl_arg_node_t* args = NULL;
	for (cdsl_arg_node_t* a = action->args; a; a = a->next) {
		args = cdsl_append_arg(args, copy_expr(a->expr));
	}
	return cdsl_create_action(strdup(action->action_name), args);
}

cdsl_metric_node_t*
copy_metric_list(const cdsl_metric_node_t* src)
{
	if (!src) {
		return NULL;
	}
	cdsl_metric_node_t* head = NULL;
	cdsl_metric_node_t* tail = NULL;
	for (const cdsl_metric_node_t* m = src; m; m = m->next) {
		cdsl_meta_item_t* meta = NULL;
		for (cdsl_meta_item_t* mi = m->meta_list; mi; mi = mi->next) {
			char* k = strdup(mi->key);
			char* v = strdup(mi->value);
			if (k && v) {
				meta = cdsl_append_meta(meta, cdsl_create_meta_item(k, v));
			} else {
				free(k);
				free(v);
			}
		}

		cdsl_case_node_t* cases = NULL;
		for (cdsl_case_node_t* c = m->case_list; c; c = c->next) {
			cases = cdsl_append_case(
			    cases,
			    cdsl_create_case(copy_expr(c->condition), copy_action(c->action)));
		}

		cdsl_metric_node_t* nm = cdsl_create_metric(
		    strdup(m->name), meta, cases, copy_action(m->default_action));
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
static cdsl_rwlock_t template_lock = CDSL_RWLOCK_INITIALIZER;

void
cdsl_template_register(cdsl_rule_t* template_rule)
{
	if (!template_rule || !template_rule->name) {
		return;
	}
	cdsl_template_entry_t* e = calloc(1, sizeof(*e));
	if (!e) {
		return;
	}
	e->name = strdup(template_rule->name);
	if (!e->name) {
		free(e);
		return;
	}
	e->rule = template_rule;
	CDSL_RWLOCK_WRLOCK(&template_lock);
	e->next = template_registry;
	template_registry = e;
	CDSL_RWLOCK_UNLOCK_WR(&template_lock);
}

cdsl_rule_t*
cdsl_template_get(const char* name)
{
	CDSL_RWLOCK_RDLOCK(&template_lock);
	for (cdsl_template_entry_t* e = template_registry; e; e = e->next) {
		if (strcmp(e->name, name) == 0) {
			cdsl_rule_t* r = e->rule;
			CDSL_RWLOCK_UNLOCK_RD(&template_lock);
			return r;
		}
	}
	CDSL_RWLOCK_UNLOCK_RD(&template_lock);
	return NULL;
}

void
cdsl_template_clear(void)
{
	CDSL_RWLOCK_WRLOCK(&template_lock);
	cdsl_template_entry_t* e = template_registry;
	template_registry = NULL;
	CDSL_RWLOCK_UNLOCK_WR(&template_lock);
	while (e) {
		cdsl_template_entry_t* next = e->next;
		cdsl_free_rule(e->rule);
		free(e->name);
		free(e);
		e = next;
	}
}
/** @} */
