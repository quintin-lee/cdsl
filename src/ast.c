#include "ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static char*
dup_str(const char* s)
{
	if (!s) {
		return NULL;
	}
	size_t len = strlen(s);
	char* d = malloc(len + 1);
	memcpy(d, s, len + 1);
	return d;
}

cdsl_expr_node_t*
cdsl_create_expr_id(char* id)
{
	cdsl_expr_node_t* n = calloc(1, sizeof(*n));
	n->type = CDSL_EXPR_ID;
	n->data.id_val = id;
	return n;
}

cdsl_expr_node_t*
cdsl_create_expr_int(int val)
{
	cdsl_expr_node_t* n = calloc(1, sizeof(*n));
	n->type = CDSL_EXPR_INT;
	n->data.int_val = val;
	return n;
}

cdsl_expr_node_t*
cdsl_create_expr_float(double val)
{
	cdsl_expr_node_t* n = calloc(1, sizeof(*n));
	n->type = CDSL_EXPR_FLOAT;
	n->data.float_val = val;
	return n;
}

cdsl_expr_node_t*
cdsl_create_expr_bool(int val)
{
	cdsl_expr_node_t* n = calloc(1, sizeof(*n));
	n->type = CDSL_EXPR_BOOL;
	n->data.bool_val = val;
	return n;
}

cdsl_expr_node_t*
cdsl_create_expr_string(char* val)
{
	cdsl_expr_node_t* n = calloc(1, sizeof(*n));
	n->type = CDSL_EXPR_STRING;
	n->data.string_val = val;
	return n;
}

cdsl_expr_node_t*
cdsl_create_expr_binary(cdsl_op_t op, cdsl_expr_node_t* left, cdsl_expr_node_t* right)
{
	cdsl_expr_node_t* n = calloc(1, sizeof(*n));
	n->type = CDSL_EXPR_BINARY;
	n->data.binary.op = op;
	n->data.binary.left = left;
	n->data.binary.right = right;
	return n;
}

cdsl_expr_node_t*
cdsl_create_expr_unary(cdsl_op_t op, cdsl_expr_node_t* expr)
{
	cdsl_expr_node_t* n = calloc(1, sizeof(*n));
	n->type = CDSL_EXPR_UNARY;
	n->data.unary.op = op;
	n->data.unary.expr = expr;
	return n;
}

cdsl_expr_node_t*
cdsl_create_expr_call(char* func_name, cdsl_arg_node_t* args)
{
	cdsl_expr_node_t* n = calloc(1, sizeof(*n));
	n->type = CDSL_EXPR_CALL;
	n->data.call.func_name = func_name;
	n->data.call.args = args;
	return n;
}

cdsl_arg_node_t*
cdsl_create_arg(cdsl_expr_node_t* expr)
{
	cdsl_arg_node_t* a = calloc(1, sizeof(*a));
	a->expr = expr;
	return a;
}

cdsl_arg_node_t*
cdsl_append_arg(cdsl_arg_node_t* list, cdsl_expr_node_t* expr)
{
	cdsl_arg_node_t* new_node = cdsl_create_arg(expr);
	if (!list) {
		return new_node;
	}
	cdsl_arg_node_t* cur = list;
	while (cur->next) {
		cur = cur->next;
	}
	cur->next = new_node;
	return list;
}

cdsl_action_node_t*
cdsl_create_action(char* name, cdsl_arg_node_t* args)
{
	cdsl_action_node_t* a = calloc(1, sizeof(*a));
	a->action_name = name;
	a->args = args;
	return a;
}

cdsl_meta_item_t*
cdsl_create_meta_item(char* key, char* value)
{
	cdsl_meta_item_t* m = calloc(1, sizeof(*m));
	m->key = key;
	m->value = value;
	return m;
}

cdsl_meta_item_t*
cdsl_append_meta(cdsl_meta_item_t* list, cdsl_meta_item_t* item)
{
	if (!list) {
		return item;
	}
	item->next = list;
	return item;
}

cdsl_case_node_t*
cdsl_create_case(cdsl_expr_node_t* cond, cdsl_action_node_t* action)
{
	cdsl_case_node_t* c = calloc(1, sizeof(*c));
	c->condition = cond;
	c->action = action;
	return c;
}

cdsl_case_node_t*
cdsl_append_case(cdsl_case_node_t* list, cdsl_case_node_t* item)
{
	if (!list) {
		return item;
	}
	item->next = list;
	return item;
}

cdsl_metric_node_t*
cdsl_create_metric(char* name,
		   cdsl_meta_item_t* meta,
		   cdsl_case_node_t* cases,
		   cdsl_action_node_t* def_act)
{
	cdsl_metric_node_t* m = calloc(1, sizeof(*m));
	m->name = name;
	m->meta_list = meta;
	// Reverse case list to restore DSL order (bison prepends, reversing order)
	cdsl_case_node_t* prev = NULL;
	while (cases) {
		cdsl_case_node_t* next = cases->next;
		cases->next = prev;
		prev = cases;
		cases = next;
	}
	m->case_list = prev;
	m->default_action = def_act;
	return m;
}

cdsl_metric_node_t*
cdsl_append_metric(cdsl_metric_node_t* list, cdsl_metric_node_t* item)
{
	if (!list) {
		return item;
	}
	item->next = list;
	return item;
}

cdsl_rule_t*
cdsl_create_simple_rule(char* name,
			cdsl_meta_item_t* meta,
			cdsl_expr_node_t* when,
			cdsl_action_node_t* then)
{
	cdsl_rule_t* r = calloc(1, sizeof(*r));
	r->name = name;
	r->meta_list = meta;
	r->when_expr = when;
	r->then_action = then;
	return r;
}

cdsl_rule_t*
cdsl_create_metric_rule(char* name, cdsl_meta_item_t* meta, cdsl_metric_node_t* metrics)
{
	cdsl_rule_t* r = calloc(1, sizeof(*r));
	r->name = name;
	r->meta_list = meta;
	r->metrics = metrics;
	return r;
}

char*
cdsl_meta_get(cdsl_meta_item_t* list, const char* key)
{
	for (cdsl_meta_item_t* m = list; m; m = m->next) {
		if (m->key && strcmp(m->key, key) == 0) {
			return m->value;
		}
	}
	return NULL;
}

void
cdsl_free_expr(cdsl_expr_node_t* expr)
{
	if (!expr) {
		return;
	}
	switch (expr->type) {
	case CDSL_EXPR_ID:
		free(expr->data.id_val);
		break;
	case CDSL_EXPR_STRING:
		free(expr->data.string_val);
		break;
	case CDSL_EXPR_BINARY:
		cdsl_free_expr(expr->data.binary.left);
		cdsl_free_expr(expr->data.binary.right);
		break;
	case CDSL_EXPR_UNARY:
		cdsl_free_expr(expr->data.unary.expr);
		break;
	case CDSL_EXPR_CALL:
		free(expr->data.call.func_name);
		cdsl_free_arg(expr->data.call.args);
		break;
	default:
		break;
	}
	free(expr);
}

void
cdsl_free_arg(cdsl_arg_node_t* arg)
{
	while (arg) {
		cdsl_arg_node_t* next = arg->next;
		cdsl_free_expr(arg->expr);
		free(arg);
		arg = next;
	}
}

void
cdsl_free_action(cdsl_action_node_t* action)
{
	if (!action) {
		return;
	}
	free(action->action_name);
	cdsl_free_arg(action->args);
	free(action);
}

void
cdsl_free_meta(cdsl_meta_item_t* meta)
{
	while (meta) {
		cdsl_meta_item_t* next = meta->next;
		free(meta->key);
		free(meta->value);
		free(meta);
		meta = next;
	}
}

void
cdsl_free_case(cdsl_case_node_t* cs)
{
	while (cs) {
		cdsl_case_node_t* next = cs->next;
		cdsl_free_expr(cs->condition);
		cdsl_free_action(cs->action);
		free(cs);
		cs = next;
	}
}

void
cdsl_free_metric(cdsl_metric_node_t* m)
{
	while (m) {
		cdsl_metric_node_t* next = m->next;
		free(m->name);
		cdsl_free_meta(m->meta_list);
		cdsl_free_case(m->case_list);
		cdsl_free_action(m->default_action);
		free(m);
		m = next;
	}
}

void
cdsl_free_rule(cdsl_rule_t* rule)
{
	if (!rule) {
		return;
	}
	free(rule->name);
	cdsl_free_meta(rule->meta_list);
	cdsl_free_expr(rule->when_expr);
	cdsl_free_action(rule->then_action);
	cdsl_free_metric(rule->metrics);
	free(rule);
}

extern cdsl_rule_t* final_parsed_rule;
extern void* yy_scan_string(const char*);
extern void yy_delete_buffer(void*);
extern int yyparse(void);

cdsl_rule_t*
cdsl_parse_string(const char* dsl_code)
{
	if (!dsl_code) {
		return NULL;
	}
	final_parsed_rule = NULL;
	void* buf = yy_scan_string(dsl_code);
	yyparse();
	yy_delete_buffer(buf);
	return final_parsed_rule;
}

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
			cdsl_meta_item_t* nmi =
			    cdsl_create_meta_item(strdup(mi->key), strdup(mi->value));
			nm->meta_list = cdsl_append_meta(nm->meta_list, nmi);
		}
		nm->case_list = NULL;
		for (cdsl_case_node_t* c = m->case_list; c; c = c->next) {
			nm->case_list = cdsl_append_case(nm->case_list,
							 cdsl_create_case(c->condition, c->action));
		}
		nm->default_action = m->default_action;
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

cdsl_rule_t*
cdsl_create_extends_rule(char* name, char* template_name, cdsl_meta_item_t* meta)
{
	cdsl_rule_t* tpl = cdsl_template_get(template_name);
	if (!tpl) {
		fprintf(stderr, "Template '%s' not found\n", template_name);
		free(name);
		cdsl_free_meta(meta);
		return NULL;
	}
	cdsl_rule_t* rule = calloc(1, sizeof(*rule));
	rule->name = name;
	rule->meta_list = meta;
	rule->metrics = copy_metric_list(tpl->metrics);
	return rule;
}
