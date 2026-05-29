#ifndef CDSL_ABSTRACT_H
#define CDSL_ABSTRACT_H

#include "ast.h"

typedef struct cdsl_var_schema {
    char* name;
    cdsl_type_t type;
    struct cdsl_var_schema* next;
} cdsl_var_schema_t;

typedef struct cdsl_action_schema {
    char* name;
    cdsl_type_t return_type;
    int arg_count;
    cdsl_type_t* arg_types;
    struct cdsl_action_schema* next;
} cdsl_action_schema_t;

typedef struct cdsl_schema {
    cdsl_var_schema_t* vars;
    cdsl_action_schema_t* actions;
} cdsl_schema_t;

cdsl_schema_t* cdsl_schema_create(void);
void cdsl_schema_free(cdsl_schema_t* schema);
void cdsl_schema_register_var(cdsl_schema_t* schema, const char* name, cdsl_type_t type);
void cdsl_schema_register_action(cdsl_schema_t* schema, const char* name,
                                  cdsl_type_t ret_type, int arg_count, ...);
int cdsl_verify_rule(const cdsl_rule_t* rule, const cdsl_schema_t* schema,
                      char* err_buf, int err_buf_sz);

#endif
