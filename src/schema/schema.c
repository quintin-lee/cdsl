/**
 * @file src/schema/schema.c
 * @brief Schema verification and type resolution implementation.
 *
 * @ingroup cdsl_schema
 * @defgroup cdsl_schema_impl Schema implementation
 * @{
 */

#include "cdsl/schema.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

cdsl_schema_t*
cdsl_schema_create(void)
{
	cdsl_schema_t* s = calloc(1, sizeof(*s));
	if (!s) {
		return NULL;
	}
	s->var_map = cdsl_hashmap_create(32);
	s->action_map = cdsl_hashmap_create(16);
	if (!s->var_map || !s->action_map) {
		cdsl_hashmap_free(s->var_map, NULL);
		cdsl_hashmap_free(s->action_map, NULL);
		free(s);
		return NULL;
	}
	return s;
}

void
cdsl_schema_free(cdsl_schema_t* schema)
{
	if (!schema) {
		return;
	}
	cdsl_hashmap_free(schema->var_map, NULL);
	cdsl_hashmap_free(schema->action_map, NULL);
	cdsl_var_schema_t* v = schema->vars;
	while (v) {
		cdsl_var_schema_t* next = v->next;
		free(v->name);
		free(v);
		v = next;
	}
	cdsl_action_schema_t* a = schema->actions;
	while (a) {
		cdsl_action_schema_t* next = a->next;
		free(a->name);
		free(a->arg_types);
		free(a);
		a = next;
	}
	free(schema);
}

void
cdsl_schema_register_var_rw(cdsl_schema_t* schema, const char* name, cdsl_type_t type, int readonly)
{
	if (!schema || !name) {
		return;
	}
	cdsl_var_schema_t* cur = cdsl_hashmap_get(schema->var_map, name);
	if (cur) {
		cur->type = type;
		cur->is_readonly = readonly;
		return;
	}
	cdsl_var_schema_t* v = calloc(1, sizeof(*v));
	if (!v) {
		return;
	}
	v->name = strdup(name);
	if (!v->name) {
		free(v);
		return;
	}
	v->type = type;
	v->is_readonly = readonly;
	v->next = schema->vars;
	schema->vars = v;
	cdsl_hashmap_put(schema->var_map, name, v);
}
void
cdsl_schema_register_var(cdsl_schema_t* schema, const char* name, cdsl_type_t type)
{
	cdsl_schema_register_var_rw(schema, name, type, 0);
}

void
cdsl_schema_register_action(
    cdsl_schema_t* schema, const char* name, cdsl_type_t ret_type, int arg_count, ...)
{
	cdsl_action_schema_t* a = calloc(1, sizeof(*a));
	if (!a) {
		return;
	}
	a->name = strdup(name);
	if (!a->name) {
		free(a);
		return;
	}
	a->return_type = ret_type;
	a->arg_count = arg_count;
	if (arg_count > 0) {
		a->arg_types = malloc(sizeof(cdsl_type_t) * arg_count);
		if (!a->arg_types) {
			free(a->name);
			free(a);
			return;
		}
		va_list ap;
		va_start(ap, arg_count);
		for (int i = 0; i < arg_count; i++) {
			a->arg_types[i] = (cdsl_type_t)va_arg(ap, int);
		}
		va_end(ap);
	}
	a->next = schema->actions;
	schema->actions = a;
	cdsl_hashmap_put(schema->action_map, name, a);
}

static cdsl_var_schema_t*
find_var(const cdsl_schema_t* schema, const char* name)
{
	return cdsl_hashmap_get(schema->var_map, name);
}

static cdsl_action_schema_t*
find_action(const cdsl_schema_t* schema, const char* name)
{
	return cdsl_hashmap_get(schema->action_map, name);
}

static int
count_args(cdsl_arg_node_t* args)
{
	int c = 0;
	for (cdsl_arg_node_t* a = args; a; a = a->next) {
		c++;
	}
	return c;
}

static cdsl_type_t
resolve_expr_type(cdsl_expr_node_t* expr,
		  const cdsl_schema_t* schema,
		  char* err,
		  int errsz,
		  cdsl_error_list_t* error_list)
{
	if (!expr) {
		return CDSL_TYPE_VOID;
	}

#define REPORT_ERR(kind, msg, hint)                                                                \
	do {                                                                                       \
		if (err && errsz > 0)                                                              \
			snprintf(err, errsz, "%s", msg);                                           \
		if (error_list)                                                                    \
			cdsl_error_list_add(error_list, cdsl_error_create(kind, 0, 0, msg, hint)); \
	} while (0)

	switch (expr->type) {
	case CDSL_EXPR_INT:
		return CDSL_TYPE_INT;
	case CDSL_EXPR_FLOAT:
		return CDSL_TYPE_FLOAT;
	case CDSL_EXPR_BOOL:
		return CDSL_TYPE_BOOL;
	case CDSL_EXPR_STRING:
		return CDSL_TYPE_STRING;
	case CDSL_EXPR_DATE:
		return CDSL_TYPE_DATE;
	case CDSL_EXPR_ID: {
		cdsl_var_schema_t* v = find_var(schema, expr->data.id_val);
		if (!v) {
			char msg[256];
			snprintf(msg, sizeof(msg), "Unknown variable '%s'", expr->data.id_val);
			REPORT_ERR(CDSL_ERR_SEMANTIC, msg, "Check schema registration");
			return CDSL_TYPE_VOID;
		}
		return v->type;
	}
	case CDSL_EXPR_UNARY: {
		cdsl_type_t t =
		    resolve_expr_type(expr->data.unary.expr, schema, err, errsz, error_list);
		if (t == CDSL_TYPE_VOID) {
			return CDSL_TYPE_VOID;
		}
		if (expr->data.unary.op == CDSL_OP_NOT) {
			if (t != CDSL_TYPE_BOOL) {
				REPORT_ERR(
				    CDSL_ERR_TYPE, "NOT operator requires boolean operand", NULL);
				return CDSL_TYPE_VOID;
			}
			return CDSL_TYPE_BOOL;
		}
		if (expr->data.unary.op == CDSL_OP_NEG) {
			if (t != CDSL_TYPE_INT && t != CDSL_TYPE_FLOAT && t != CDSL_TYPE_LONG) {
				REPORT_ERR(CDSL_ERR_TYPE,
					   "Negation operator requires numeric operand",
					   NULL);
				return CDSL_TYPE_VOID;
			}
			return t;
		}
		return t;
	}
	case CDSL_EXPR_BINARY: {
		cdsl_type_t lt =
		    resolve_expr_type(expr->data.binary.left, schema, err, errsz, error_list);
		cdsl_type_t rt =
		    resolve_expr_type(expr->data.binary.right, schema, err, errsz, error_list);

		if (lt == CDSL_TYPE_VOID || rt == CDSL_TYPE_VOID) {
			return CDSL_TYPE_VOID;
		}

		cdsl_op_t op = expr->data.binary.op;

		/* Logical operators (AND, OR) */
		if (op >= CDSL_OP_AND && op <= CDSL_OP_OR) {
			if (lt != CDSL_TYPE_BOOL || rt != CDSL_TYPE_BOOL) {
				REPORT_ERR(CDSL_ERR_TYPE,
					   "Logical operators require boolean operands",
					   NULL);
				return CDSL_TYPE_VOID;
			}
			return CDSL_TYPE_BOOL;
		}

		/* Arithmetic operators (+, -, *, /) */
		if (op >= CDSL_OP_ADD && op <= CDSL_OP_DIV) {
			/* DATE arithmetic: DATE +/- INT/LONG → DATE, DATE - DATE → INT */
			if (lt == CDSL_TYPE_DATE) {
				if (op == CDSL_OP_SUB && rt == CDSL_TYPE_DATE) {
					return CDSL_TYPE_INT;
				}
				if ((op == CDSL_OP_ADD || op == CDSL_OP_SUB) &&
				    (rt == CDSL_TYPE_INT || rt == CDSL_TYPE_LONG ||
				     rt == CDSL_TYPE_FLOAT)) {
					return CDSL_TYPE_DATE;
				}
				REPORT_ERR(CDSL_ERR_TYPE,
					   "DATE arithmetic requires INT/LONG or DATE operand",
					   NULL);
				return CDSL_TYPE_VOID;
			}
			if (rt == CDSL_TYPE_DATE && op == CDSL_OP_ADD &&
			    (lt == CDSL_TYPE_INT || lt == CDSL_TYPE_LONG ||
			     lt == CDSL_TYPE_FLOAT)) {
				return CDSL_TYPE_DATE;
			}
			/* MUL/DIV are numeric-only */
			if (op == CDSL_OP_MUL || op == CDSL_OP_DIV) {
				if (lt == CDSL_TYPE_DATE || rt == CDSL_TYPE_DATE) {
					REPORT_ERR(CDSL_ERR_TYPE,
						   "DATE does not support multiplication/division",
						   NULL);
					return CDSL_TYPE_VOID;
				}
			}
			if ((lt != CDSL_TYPE_INT && lt != CDSL_TYPE_FLOAT &&
			     lt != CDSL_TYPE_LONG) ||
			    (rt != CDSL_TYPE_INT && rt != CDSL_TYPE_FLOAT &&
			     rt != CDSL_TYPE_LONG)) {
				REPORT_ERR(CDSL_ERR_TYPE,
					   "Arithmetic operators require numeric operands",
					   NULL);
				return CDSL_TYPE_VOID;
			}
			return (lt == CDSL_TYPE_FLOAT || rt == CDSL_TYPE_FLOAT) ? CDSL_TYPE_FLOAT
										: CDSL_TYPE_INT;
		}

		/* Comparison operators (==, !=, <, >, <=, >=) */
		/* Allow mixing INT, FLOAT, LONG for numeric comparisons */
		if (lt != rt && !(lt == CDSL_TYPE_INT && rt == CDSL_TYPE_FLOAT) &&
		    !(lt == CDSL_TYPE_FLOAT && rt == CDSL_TYPE_INT) &&
		    !(lt == CDSL_TYPE_INT && rt == CDSL_TYPE_LONG) &&
		    !(lt == CDSL_TYPE_LONG && rt == CDSL_TYPE_INT) &&
		    !(lt == CDSL_TYPE_FLOAT && rt == CDSL_TYPE_LONG) &&
		    !(lt == CDSL_TYPE_LONG && rt == CDSL_TYPE_FLOAT)) {
			REPORT_ERR(
			    CDSL_ERR_TYPE, "Type mismatch: cannot compare different types", NULL);
			return CDSL_TYPE_VOID;
		}

		if (lt == CDSL_TYPE_STRING && op != CDSL_OP_EQ && op != CDSL_OP_NE) {
			REPORT_ERR(CDSL_ERR_TYPE,
				   "String only supports equality comparisons (==, !=)",
				   NULL);
			return CDSL_TYPE_VOID;
		}

		return CDSL_TYPE_BOOL;
	}
	case CDSL_EXPR_CALL: {
		cdsl_action_schema_t* a = find_action(schema, expr->data.call.func_name);
		if (!a) {
			char msg[256];
			snprintf(
			    msg, sizeof(msg), "Unknown function '%s'", expr->data.call.func_name);
			REPORT_ERR(CDSL_ERR_SEMANTIC, msg, "Register function in schema");
			return CDSL_TYPE_VOID;
		}
		int nargs = count_args(expr->data.call.args);
		if (nargs != a->arg_count) {
			char msg[256];
			snprintf(msg,
				 sizeof(msg),
				 "Function '%s' expects %d args, got %d",
				 expr->data.call.func_name,
				 a->arg_count,
				 nargs);
			REPORT_ERR(CDSL_ERR_TYPE, msg, NULL);
			return CDSL_TYPE_VOID;
		}
		cdsl_arg_node_t* arg = expr->data.call.args;
		for (int i = 0; i < nargs; i++) {
			cdsl_type_t t =
			    resolve_expr_type(arg->expr, schema, err, errsz, error_list);
			if (t == CDSL_TYPE_VOID) {
				return CDSL_TYPE_VOID;
			}
			if (a->arg_types[i] != t &&
			    !(a->arg_types[i] == CDSL_TYPE_FLOAT && t == CDSL_TYPE_INT)) {
				char msg[256];
				snprintf(msg,
					 sizeof(msg),
					 "Arg %d type mismatch for function '%s'",
					 i + 1,
					 expr->data.call.func_name);
				REPORT_ERR(CDSL_ERR_TYPE, msg, NULL);
				return CDSL_TYPE_VOID;
			}
			arg = arg->next;
		}
		return a->return_type;
	}
	default:
		return CDSL_TYPE_VOID;
	}
#undef REPORT_ERR
}

static bool
verify_action(cdsl_action_node_t* action,
	      const cdsl_schema_t* schema,
	      const char* context,
	      char* err,
	      int errsz,
	      cdsl_error_list_t* error_list)
{
	if (!action) {
		return true;
	}

#define REPORT_ERR(kind, msg, hint)                                                                \
	do {                                                                                       \
		if (err && errsz > 0)                                                              \
			snprintf(err, errsz, "%s: %s", context, msg);                              \
		if (error_list)                                                                    \
			cdsl_error_list_add(error_list, cdsl_error_create(kind, 0, 0, msg, hint)); \
	} while (0)

	cdsl_action_schema_t* a = find_action(schema, action->action_name);
	if (!a) {
		char hint[256];
		snprintf(hint,
			 sizeof(hint),
			 "Register action '%s' via cdsl_schema_register_action()",
			 action->action_name);
		REPORT_ERR(CDSL_ERR_SEMANTIC, "Unknown action", hint);
		return false;
	}
	int nargs = count_args(action->args);
	if (nargs != a->arg_count) {
		char msg[256];
		snprintf(msg, sizeof(msg), "Action expects %d args, got %d", a->arg_count, nargs);
		REPORT_ERR(CDSL_ERR_TYPE, msg, NULL);
		return false;
	}
	cdsl_arg_node_t* arg = action->args;
	for (int i = 0; i < nargs; i++) {
		cdsl_type_t t = resolve_expr_type(arg->expr, schema, err, errsz, error_list);
		if (t == CDSL_TYPE_VOID) {
			return false;
		}
		if (a->arg_types[i] != t &&
		    !(a->arg_types[i] == CDSL_TYPE_FLOAT && t == CDSL_TYPE_INT)) {
			char msg[256];
			snprintf(msg, sizeof(msg), "Arg %d type mismatch", i + 1);
			REPORT_ERR(CDSL_ERR_TYPE, msg, NULL);
			return false;
		}
		arg = arg->next;
	}
	return true;
#undef REPORT_ERR
}

bool
cdsl_verify_rule(const cdsl_rule_t* rule,
		 const cdsl_schema_t* schema,
		 char* err_buf,
		 int err_buf_sz)
{
	cdsl_error_list_t* errors = cdsl_verify_rule_detailed(rule, schema);
	if (!errors) {
		return false;
	}
	bool ok = !cdsl_error_list_has_errors(errors);
	if (!ok && err_buf && err_buf_sz > 0) {
		snprintf(err_buf, err_buf_sz, "%s", errors->errors[0]->message);
	}
	cdsl_error_list_free(errors);
	return ok;
}

cdsl_error_list_t*
cdsl_verify_rule_detailed(const cdsl_rule_t* rule, const cdsl_schema_t* schema)
{
	cdsl_error_list_t* errors = cdsl_error_list_create();
	if (!rule || !schema) {
		cdsl_error_list_add(
		    errors,
		    cdsl_error_create(CDSL_ERR_SEMANTIC, 0, 0, "Null rule or schema", NULL));
		return errors;
	}

	if (rule->metrics) {
		for (cdsl_metric_node_t* m = rule->metrics; m; m = m->next) {
			for (cdsl_case_node_t* c = m->case_list; c; c = c->next) {
				cdsl_type_t t =
				    resolve_expr_type(c->condition, schema, NULL, 0, errors);
				if (t != CDSL_TYPE_VOID && t != CDSL_TYPE_BOOL &&
				    t != CDSL_TYPE_INT && t != CDSL_TYPE_FLOAT &&
				    t != CDSL_TYPE_STRING && t != CDSL_TYPE_LONG &&
				    t != CDSL_TYPE_DATE) {
					cdsl_error_list_add(
					    errors,
					    cdsl_error_create(CDSL_ERR_TYPE,
							      0,
							      0,
							      "Invalid case condition type",
							      m->name));
				}
				verify_action(c->action, schema, m->name, NULL, 0, errors);
			}
			verify_action(m->default_action, schema, m->name, NULL, 0, errors);
		}
	} else {
		if (rule->when_expr) {
			resolve_expr_type(rule->when_expr, schema, NULL, 0, errors);
		}
		verify_action(rule->then_action, schema, rule->name, NULL, 0, errors);
	}
	return errors;
}

/* ---- static analysis ---- */

#define ANALYZE_WARN(list, msg, hint)                                                              \
	cdsl_error_list_add((list), cdsl_error_create(CDSL_ERR_WARNING, 0, 0, (msg), (hint)))

static int
expr_is_const_true(cdsl_expr_node_t* expr)
{
	if (!expr) {
		return 0;
	}
	switch (expr->type) {
	case CDSL_EXPR_BOOL:
		return expr->data.bool_val;
	case CDSL_EXPR_INT:
		return expr->data.int_val != 0;
	case CDSL_EXPR_BINARY: {
		cdsl_op_t op = expr->data.binary.op;
		cdsl_expr_node_t *l = expr->data.binary.left, *r = expr->data.binary.right;
		if (!l || !r) {
			return 0;
		}
		if (l->type != CDSL_EXPR_INT || r->type != CDSL_EXPR_INT) {
			return 0;
		}
		switch (op) {
		case CDSL_OP_EQ:
			return l->data.int_val == r->data.int_val;
		case CDSL_OP_NE:
			return l->data.int_val != r->data.int_val;
		case CDSL_OP_LT:
			return l->data.int_val < r->data.int_val;
		case CDSL_OP_GT:
			return l->data.int_val > r->data.int_val;
		case CDSL_OP_LE:
			return l->data.int_val <= r->data.int_val;
		case CDSL_OP_GE:
			return l->data.int_val >= r->data.int_val;
		default:
			return 0;
		}
	}
	default:
		return 0;
	}
}

static int
expr_is_const_false(cdsl_expr_node_t* expr)
{
	if (!expr) {
		return 0;
	}
	switch (expr->type) {
	case CDSL_EXPR_BOOL:
		return !expr->data.bool_val;
	case CDSL_EXPR_INT:
		return expr->data.int_val == 0;
	case CDSL_EXPR_BINARY: {
		cdsl_op_t op = expr->data.binary.op;
		cdsl_expr_node_t *l = expr->data.binary.left, *r = expr->data.binary.right;
		if (!l || !r) {
			return 0;
		}
		if (l->type != CDSL_EXPR_INT || r->type != CDSL_EXPR_INT) {
			return 0;
		}
		switch (op) {
		case CDSL_OP_EQ:
			return l->data.int_val != r->data.int_val;
		case CDSL_OP_NE:
			return l->data.int_val == r->data.int_val;
		case CDSL_OP_LT:
			return l->data.int_val >= r->data.int_val;
		case CDSL_OP_GT:
			return l->data.int_val <= r->data.int_val;
		case CDSL_OP_LE:
			return l->data.int_val > r->data.int_val;
		case CDSL_OP_GE:
			return l->data.int_val < r->data.int_val;
		default:
			return 0;
		}
	}
	default:
		return 0;
	}
}

cdsl_error_list_t*
cdsl_analyze_rule(const cdsl_rule_t* rule, const cdsl_schema_t* schema)
{
	(void)schema;
	if (!rule) {
		return NULL;
	}

	cdsl_error_list_t* w = cdsl_error_list_create();
	if (!w) {
		return NULL;
	}

	/* Dead CASE detection in metric rules */
	if (rule->metrics) {
		for (cdsl_metric_node_t* m = rule->metrics; m; m = m->next) {
			int has_always_true = 0;
			cdsl_case_node_t* first = m->case_list;
			for (cdsl_case_node_t* c = m->case_list; c; c = c->next) {
				if (!c->condition) {
					continue;
				}
				if (has_always_true) {
					char buf[200];
					snprintf(buf,
						 sizeof(buf),
						 "CASE in '%s' unreachable: "
						 "preceded by always-true condition",
						 m->name);
					ANALYZE_WARN(w, buf, "Reorder or remove CASE branches");
					break;
				}
				if (expr_is_const_true(c->condition)) {
					char buf[200];
					snprintf(buf,
						 sizeof(buf),
						 "CASE in '%s' is always true; "
						 "subsequent branches are dead code",
						 m->name);
					ANALYZE_WARN(w, buf, "Consider using DEFAULT instead");
					has_always_true = 1;
				} else if (expr_is_const_false(c->condition)) {
					char buf[200];
					snprintf(buf,
						 sizeof(buf),
						 "CASE in '%s' is always false; dead code",
						 m->name);
					ANALYZE_WARN(w, buf, "Remove or fix the condition");
				}
				/* Shadowed CASE: identical condition on same variable */
				for (cdsl_case_node_t* d = first; d != c && !has_always_true;
				     d = d->next) {
					if (!d->condition || !c->condition) {
						continue;
					}
					if (d->condition->type != CDSL_EXPR_BINARY ||
					    c->condition->type != CDSL_EXPR_BINARY) {
						continue;
					}
					cdsl_op_t op1 = d->condition->data.binary.op;
					cdsl_op_t op2 = c->condition->data.binary.op;
					cdsl_expr_node_t *l1, *r1, *l2, *r2;
					l1 = d->condition->data.binary.left;
					r1 = d->condition->data.binary.right;
					l2 = c->condition->data.binary.left;
					r2 = c->condition->data.binary.right;
					if (!l1 || !r1 || !l2 || !r2) {
						continue;
					}
					if (op1 != op2) {
						continue;
					}
					if (l1->type != CDSL_EXPR_ID || l2->type != CDSL_EXPR_ID) {
						continue;
					}
					if (strcmp(l1->data.id_val, l2->data.id_val) != 0) {
						continue;
					}
					int same_val = 0;
					if (r1->type == CDSL_EXPR_INT &&
					    r2->type == CDSL_EXPR_INT) {
						same_val = r1->data.int_val == r2->data.int_val;
					}
					if (r1->type == CDSL_EXPR_FLOAT &&
					    r2->type == CDSL_EXPR_FLOAT) {
						same_val = r1->data.float_val == r2->data.float_val;
					}
					if (same_val) {
						char buf[200];
						snprintf(buf,
							 sizeof(buf),
							 "CASE in '%s' shadows earlier identical "
							 "condition; branch is unreachable",
							 m->name);
						ANALYZE_WARN(w, buf, "Remove duplicate CASE");
						break;
					}
				}
			}
		}
	}

	/* Tautology/contradiction in simple rules */
	if (rule->when_expr) {
		if (expr_is_const_true(rule->when_expr)) {
			ANALYZE_WARN(w,
				     "WHEN condition is always true; rule triggers on every "
				     "evaluation",
				     "Review condition logic");
		}
		if (expr_is_const_false(rule->when_expr)) {
			ANALYZE_WARN(w,
				     "WHEN condition is always false; rule never triggers",
				     "Review condition logic");
		}
	}

	if (w->count == 0) {
		cdsl_error_list_free(w);
		return NULL;
	}
	return w;
}

#undef ANALYZE_WARN
/** @} */
