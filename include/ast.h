#ifndef CDSL_AST_H
#define CDSL_AST_H

#include <stddef.h>

typedef enum {
    CDSL_TYPE_INT,
    CDSL_TYPE_FLOAT,
    CDSL_TYPE_BOOL,
    CDSL_TYPE_STRING,
    CDSL_TYPE_VOID
} cdsl_type_t;

typedef enum {
    CDSL_OP_EQ, CDSL_OP_NE, CDSL_OP_LT, CDSL_OP_GT,
    CDSL_OP_LE, CDSL_OP_GE,
    CDSL_OP_AND, CDSL_OP_OR, CDSL_OP_NOT
} cdsl_op_t;

typedef enum {
    CDSL_EXPR_ID,
    CDSL_EXPR_INT,
    CDSL_EXPR_FLOAT,
    CDSL_EXPR_BOOL,
    CDSL_EXPR_STRING,
    CDSL_EXPR_BINARY,
    CDSL_EXPR_UNARY
} cdsl_expr_type_t;

typedef struct cdsl_expr_node {
    cdsl_expr_type_t type;
    union {
        char* id_val;
        int int_val;
        double float_val;
        int bool_val;
        char* string_val;
        struct { cdsl_op_t op; struct cdsl_expr_node* left; struct cdsl_expr_node* right; } binary;
        struct { cdsl_op_t op; struct cdsl_expr_node* expr; } unary;
    } data;
} cdsl_expr_node_t;

typedef struct cdsl_arg_node {
    cdsl_expr_node_t* expr;
    struct cdsl_arg_node* next;
} cdsl_arg_node_t;

typedef struct cdsl_action_node {
    char* action_name;
    cdsl_arg_node_t* args;
} cdsl_action_node_t;

typedef struct cdsl_meta_item {
    char* key;
    char* value;
    struct cdsl_meta_item* next;
} cdsl_meta_item_t;

typedef struct cdsl_case_node {
    cdsl_expr_node_t* condition;
    cdsl_action_node_t* action;
    struct cdsl_case_node* next;
} cdsl_case_node_t;

typedef struct cdsl_metric_node {
    char* name;
    cdsl_meta_item_t* meta_list;
    cdsl_case_node_t* case_list;
    cdsl_action_node_t* default_action;
    struct cdsl_metric_node* next;
} cdsl_metric_node_t;

typedef struct cdsl_rule {
    char* name;
    cdsl_meta_item_t* meta_list;
    cdsl_expr_node_t* when_expr;
    cdsl_action_node_t* then_action;
    cdsl_metric_node_t* metrics;
} cdsl_rule_t;

cdsl_expr_node_t* cdsl_create_expr_id(char* id);
cdsl_expr_node_t* cdsl_create_expr_int(int val);
cdsl_expr_node_t* cdsl_create_expr_float(double val);
cdsl_expr_node_t* cdsl_create_expr_bool(int val);
cdsl_expr_node_t* cdsl_create_expr_string(char* val);
cdsl_expr_node_t* cdsl_create_expr_binary(cdsl_op_t op, cdsl_expr_node_t* left, cdsl_expr_node_t* right);
cdsl_expr_node_t* cdsl_create_expr_unary(cdsl_op_t op, cdsl_expr_node_t* expr);

cdsl_arg_node_t* cdsl_create_arg(cdsl_expr_node_t* expr);
cdsl_arg_node_t* cdsl_append_arg(cdsl_arg_node_t* list, cdsl_expr_node_t* expr);

cdsl_action_node_t* cdsl_create_action(char* name, cdsl_arg_node_t* args);

cdsl_meta_item_t* cdsl_create_meta_item(char* key, char* value);
cdsl_meta_item_t* cdsl_append_meta(cdsl_meta_item_t* list, cdsl_meta_item_t* item);

cdsl_case_node_t* cdsl_create_case(cdsl_expr_node_t* cond, cdsl_action_node_t* action);
cdsl_case_node_t* cdsl_append_case(cdsl_case_node_t* list, cdsl_case_node_t* item);

cdsl_metric_node_t* cdsl_create_metric(char* name, cdsl_meta_item_t* meta,
                                        cdsl_case_node_t* cases, cdsl_action_node_t* def_act);
cdsl_metric_node_t* cdsl_append_metric(cdsl_metric_node_t* list, cdsl_metric_node_t* item);

cdsl_rule_t* cdsl_create_simple_rule(char* name, cdsl_meta_item_t* meta,
                                      cdsl_expr_node_t* when, cdsl_action_node_t* then);
cdsl_rule_t* cdsl_create_metric_rule(char* name, cdsl_meta_item_t* meta,
                                      cdsl_metric_node_t* metrics);

char* cdsl_meta_get(cdsl_meta_item_t* list, const char* key);

void cdsl_free_expr(cdsl_expr_node_t* expr);
void cdsl_free_arg(cdsl_arg_node_t* arg);
void cdsl_free_action(cdsl_action_node_t* action);
void cdsl_free_meta(cdsl_meta_item_t* meta);
void cdsl_free_case(cdsl_case_node_t* cs);
void cdsl_free_metric(cdsl_metric_node_t* m);
void cdsl_free_rule(cdsl_rule_t* rule);

cdsl_rule_t* cdsl_parse_string(const char* dsl_code);

#endif
