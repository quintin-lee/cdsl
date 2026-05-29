#include "abstract.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

cdsl_schema_t* cdsl_schema_create(void) {
    cdsl_schema_t* s = calloc(1, sizeof(*s));
    return s;
}

void cdsl_schema_free(cdsl_schema_t* schema) {
    if (!schema) return;
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

void cdsl_schema_register_var(cdsl_schema_t* schema, const char* name, cdsl_type_t type) {
    cdsl_var_schema_t* v = calloc(1, sizeof(*v));
    v->name = strdup(name);
    v->type = type;
    v->next = schema->vars;
    schema->vars = v;
}

void cdsl_schema_register_action(cdsl_schema_t* schema, const char* name,
                                  cdsl_type_t ret_type, int arg_count, ...) {
    cdsl_action_schema_t* a = calloc(1, sizeof(*a));
    a->name = strdup(name);
    a->return_type = ret_type;
    a->arg_count = arg_count;
    if (arg_count > 0) {
        a->arg_types = malloc(sizeof(cdsl_type_t) * arg_count);
        va_list ap;
        va_start(ap, arg_count);
        for (int i = 0; i < arg_count; i++) {
            a->arg_types[i] = va_arg(ap, cdsl_type_t);
        }
        va_end(ap);
    }
    a->next = schema->actions;
    schema->actions = a;
}

static cdsl_var_schema_t* find_var(const cdsl_schema_t* schema, const char* name) {
    for (cdsl_var_schema_t* v = schema->vars; v; v = v->next) {
        if (strcmp(v->name, name) == 0) return v;
    }
    return NULL;
}

static cdsl_action_schema_t* find_action(const cdsl_schema_t* schema, const char* name) {
    for (cdsl_action_schema_t* a = schema->actions; a; a = a->next) {
        if (strcmp(a->name, name) == 0) return a;
    }
    return NULL;
}

static cdsl_type_t resolve_expr_type(cdsl_expr_node_t* expr, const cdsl_schema_t* schema, char* err, int errsz) {
    if (!expr) return CDSL_TYPE_VOID;
    switch (expr->type) {
        case CDSL_EXPR_INT:    return CDSL_TYPE_INT;
        case CDSL_EXPR_FLOAT:  return CDSL_TYPE_FLOAT;
        case CDSL_EXPR_BOOL:   return CDSL_TYPE_BOOL;
        case CDSL_EXPR_STRING: return CDSL_TYPE_STRING;
        case CDSL_EXPR_ID: {
            cdsl_var_schema_t* v = find_var(schema, expr->data.id_val);
            if (!v) {
                snprintf(err, errsz, "Unknown variable '%s'", expr->data.id_val);
                return CDSL_TYPE_VOID;
            }
            return v->type;
        }
        case CDSL_EXPR_UNARY: {
            cdsl_type_t t = resolve_expr_type(expr->data.unary.expr, schema, err, errsz);
            if (expr->data.unary.op == CDSL_OP_NOT) return CDSL_TYPE_BOOL;
            return t;
        }
        case CDSL_EXPR_BINARY: {
            cdsl_type_t lt = resolve_expr_type(expr->data.binary.left, schema, err, errsz);
            if (err[0]) return CDSL_TYPE_VOID;
            cdsl_type_t rt = resolve_expr_type(expr->data.binary.right, schema, err, errsz);
            if (err[0]) return CDSL_TYPE_VOID;
            if (lt == CDSL_TYPE_VOID || rt == CDSL_TYPE_VOID) {
                snprintf(err, errsz, "Invalid type in expression");
                return CDSL_TYPE_VOID;
            }
            if (expr->data.binary.op >= CDSL_OP_AND) return CDSL_TYPE_BOOL;
            if (lt != rt && !(lt == CDSL_TYPE_INT && rt == CDSL_TYPE_FLOAT) &&
                !(lt == CDSL_TYPE_FLOAT && rt == CDSL_TYPE_INT)) {
                snprintf(err, errsz, "Type mismatch: cannot compare different types");
                return CDSL_TYPE_VOID;
            }
            return CDSL_TYPE_BOOL;
        }
        default: return CDSL_TYPE_VOID;
    }
}

static int count_args(cdsl_arg_node_t* args) {
    int c = 0;
    for (cdsl_arg_node_t* a = args; a; a = a->next) c++;
    return c;
}

static int verify_action(cdsl_action_node_t* action, const cdsl_schema_t* schema,
                          const char* context, char* err, int errsz) {
    if (!action) return 1;
    cdsl_action_schema_t* a = find_action(schema, action->action_name);
    if (!a) {
        snprintf(err, errsz, "%s: Unknown action '%s'", context, action->action_name);
        return 0;
    }
    int nargs = count_args(action->args);
    if (nargs != a->arg_count) {
        snprintf(err, errsz, "%s: Action '%s' expects %d args, got %d",
                 context, action->action_name, a->arg_count, nargs);
        return 0;
    }
    cdsl_arg_node_t* arg = action->args;
    for (int i = 0; i < nargs; i++) {
        cdsl_type_t t = resolve_expr_type(arg->expr, schema, err, errsz);
        if (err[0]) return 0;
        if (a->arg_types[i] != t && !(a->arg_types[i] == CDSL_TYPE_FLOAT && t == CDSL_TYPE_INT)) {
            snprintf(err, errsz, "%s: Arg %d type mismatch for action '%s'",
                     context, i + 1, action->action_name);
            return 0;
        }
        arg = arg->next;
    }
    return 1;
}

int cdsl_verify_rule(const cdsl_rule_t* rule, const cdsl_schema_t* schema,
                      char* err_buf, int err_buf_sz) {
    if (!rule || !schema) {
        snprintf(err_buf, err_buf_sz, "Null rule or schema");
        return 0;
    }
    err_buf[0] = '\0';

    if (rule->metrics) {
        for (cdsl_metric_node_t* m = rule->metrics; m; m = m->next) {
            for (cdsl_case_node_t* c = m->case_list; c; c = c->next) {
                cdsl_type_t t = resolve_expr_type(c->condition, schema, err_buf, err_buf_sz);
                if (err_buf[0]) return 0;
                if (t != CDSL_TYPE_BOOL && t != CDSL_TYPE_INT && t != CDSL_TYPE_FLOAT &&
                    t != CDSL_TYPE_STRING) {
                    snprintf(err_buf, err_buf_sz, "Metric '%s': invalid case condition type", m->name);
                    return 0;
                }
                if (!verify_action(c->action, schema, m->name, err_buf, err_buf_sz)) return 0;
            }
            if (!verify_action(m->default_action, schema, m->name, err_buf, err_buf_sz)) return 0;
        }
        return 1;
    }

    if (rule->when_expr) {
        cdsl_type_t t = resolve_expr_type(rule->when_expr, schema, err_buf, err_buf_sz);
        if (err_buf[0]) return 0;
    }
    if (!verify_action(rule->then_action, schema, rule->name, err_buf, err_buf_sz)) return 0;

    return 1;
}
