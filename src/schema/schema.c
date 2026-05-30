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
	return calloc(1, sizeof(cdsl_schema_t));
}

void
cdsl_schema_free(cdsl_schema_t* schema)
{
	if (!schema) {
		return;
	}
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
cdsl_schema_register_var(cdsl_schema_t* schema, const char* name, cdsl_type_t type)
{
	for (cdsl_var_schema_t* cur = schema->vars; cur; cur = cur->next) {
		if (strcmp(cur->name, name) == 0) {
			cur->type = type;
			return;
		}
	}
	cdsl_var_schema_t* v = calloc(1, sizeof(*v));
	v->name = strdup(name);
	v->type = type;
	v->next = schema->vars;
	schema->vars = v;
}

void
cdsl_schema_register_action(
    cdsl_schema_t* schema, const char* name, cdsl_type_t ret_type, int arg_count, ...)
{
	cdsl_action_schema_t* a = calloc(1, sizeof(*a));
	a->name = strdup(name);
	a->return_type = ret_type;
	a->arg_count = arg_count;
	if (arg_count > 0) {
		a->arg_types = malloc(sizeof(cdsl_type_t) * arg_count);
		va_list ap;
		va_start(ap, arg_count);
		for (int i = 0; i < arg_count; i++) {
			a->arg_types[i] = (cdsl_type_t)va_arg(ap, int);
		}
		va_end(ap);
	}
	a->next = schema->actions;
	schema->actions = a;
}

static cdsl_var_schema_t*
find_var(const cdsl_schema_t* schema, const char* name)
{
	for (cdsl_var_schema_t* v = schema->vars; v; v = v->next) {
		if (strcmp(v->name, name) == 0) {
			return v;
		}
	}
	return NULL;
}

static cdsl_action_schema_t*
find_action(const cdsl_schema_t* schema, const char* name)
{
	for (cdsl_action_schema_t* a = schema->actions; a; a = a->next) {
		if (strcmp(a->name, name) == 0) {
			return a;
		}
	}
	return NULL;
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
			if (t != CDSL_TYPE_INT && t != CDSL_TYPE_FLOAT) {
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
			if ((lt != CDSL_TYPE_INT && lt != CDSL_TYPE_FLOAT) ||
			    (rt != CDSL_TYPE_INT && rt != CDSL_TYPE_FLOAT)) {
				REPORT_ERR(CDSL_ERR_TYPE,
					   "Arithmetic operators require numeric operands",
					   NULL);
				return CDSL_TYPE_VOID;
			}
			return (lt == CDSL_TYPE_FLOAT || rt == CDSL_TYPE_FLOAT) ? CDSL_TYPE_FLOAT
										: CDSL_TYPE_INT;
		}

		/* Comparison operators (==, !=, <, >, <=, >=) */
		if (lt != rt && !(lt == CDSL_TYPE_INT && rt == CDSL_TYPE_FLOAT) &&
		    !(lt == CDSL_TYPE_FLOAT && rt == CDSL_TYPE_INT)) {
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
				    t != CDSL_TYPE_STRING) {
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
/** @} */
