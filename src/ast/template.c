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
		return cdsl_create_expr_int(expr->data.int_val, expr->line, expr->column);
	case CDSL_EXPR_FLOAT:
		return cdsl_create_expr_float(expr->data.float_val, expr->line, expr->column);
	case CDSL_EXPR_BOOL:
		return cdsl_create_expr_bool(expr->data.bool_val, expr->line, expr->column);
	case CDSL_EXPR_ID: {
		char* id = cdsl_strdup(expr->data.id_val);
		if (!id) {
			return NULL;
		}
		return cdsl_create_expr_id(id, expr->line, expr->column);
	}
	case CDSL_EXPR_STRING: {
		char* str = cdsl_strdup(expr->data.string_val);
		if (!str) {
			return NULL;
		}
		return cdsl_create_expr_string(str, expr->line, expr->column);
	}
	case CDSL_EXPR_DATE:
		return cdsl_create_expr_date(expr->data.date_val, expr->line, expr->column);
	case CDSL_EXPR_LONG:
		return cdsl_create_expr_long(expr->data.long_val, expr->line, expr->column);
	case CDSL_EXPR_ARRAY: {
		cdsl_arg_node_t* elems = NULL;
		for (cdsl_arg_node_t* a = expr->data.array.elements; a; a = a->next) {
			cdsl_expr_node_t* sub = copy_expr(a->expr);
			if (!sub && a->expr) {
				cdsl_free_arg(elems);
				return NULL;
			}
			elems = cdsl_append_arg(elems, sub);
		}
		return cdsl_create_expr_array(elems, expr->line, expr->column);
	}
	case CDSL_EXPR_UNARY: {
		cdsl_expr_node_t* sub = copy_expr(expr->data.unary.expr);
		if (!sub && expr->data.unary.expr) {
			return NULL;
		}
		return cdsl_create_expr_unary(expr->data.unary.op, sub, expr->line, expr->column);
	}
	case CDSL_EXPR_BINARY: {
		cdsl_expr_node_t* left = copy_expr(expr->data.binary.left);
		cdsl_expr_node_t* right = copy_expr(expr->data.binary.right);
		if ((!left && expr->data.binary.left) || (!right && expr->data.binary.right)) {
			cdsl_free_expr(left);
			cdsl_free_expr(right);
			return NULL;
		}
		return cdsl_create_expr_binary(
		    expr->data.binary.op, left, right, expr->line, expr->column);
	}
	case CDSL_EXPR_CALL: {
		char* fname = cdsl_strdup(expr->data.call.func_name);
		if (!fname) {
			return NULL;
		}
		cdsl_arg_node_t* args = NULL;
		for (cdsl_arg_node_t* a = expr->data.call.args; a; a = a->next) {
			cdsl_expr_node_t* sub = copy_expr(a->expr);
			if (!sub && a->expr) {
				free(fname);
				cdsl_free_arg(args);
				return NULL;
			}
			args = cdsl_append_arg(args, sub);
		}
		return cdsl_create_expr_call(fname, args, expr->line, expr->column);
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
	char* aname = cdsl_strdup(action->action_name);
	if (!aname) {
		return NULL;
	}
	cdsl_arg_node_t* args = NULL;
	for (cdsl_arg_node_t* a = action->args; a; a = a->next) {
		cdsl_expr_node_t* sub = copy_expr(a->expr);
		if (!sub && a->expr) {
			free(aname);
			cdsl_free_arg(args);
			return NULL;
		}
		args = cdsl_append_arg(args, sub);
	}
	return cdsl_create_action(aname, args, action->line, action->column);
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
		char* mname = cdsl_strdup(m->name);
		if (!mname) {
			cdsl_free_metric(head);
			return NULL;
		}
		cdsl_meta_item_t* meta = NULL;
		for (cdsl_meta_item_t* mi = m->meta_list; mi; mi = mi->next) {
			meta = cdsl_append_meta_kv(meta, mi->key, mi->value);
		}

		cdsl_case_node_t* cases = NULL;
		for (cdsl_case_node_t* c = m->case_list; c; c = c->next) {
			cases = cdsl_append_case(
			    cases,
			    cdsl_create_case(copy_expr(c->condition), copy_action(c->action)));
		}

		cdsl_metric_node_t* nm =
		    cdsl_create_metric(mname, meta, cases, copy_action(m->default_action));
		if (!nm) {
			free(mname);
			cdsl_free_meta(meta);
			cdsl_free_case(cases);
			cdsl_free_metric(head);
			return NULL;
		}
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
	e->name = cdsl_strdup(template_rule->name);
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
