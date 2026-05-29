#ifndef CDSL_EXECUTION_H
#define CDSL_EXECUTION_H

#include "ast.h"
#include "abstract.h"

typedef struct cdsl_value {
    cdsl_type_t type;
    union {
        int int_val;
        double float_val;
        int bool_val;
        char* string_val;
    } data;
} cdsl_value_t;

typedef struct cdsl_context_entry {
    char* name;
    cdsl_value_t value;
    struct cdsl_context_entry* next;
} cdsl_context_entry_t;

typedef struct cdsl_context {
    const cdsl_schema_t* schema;
    cdsl_context_entry_t* entries;
} cdsl_context_t;

typedef void (*cdsl_action_cb_t)(const char* action_name, cdsl_arg_node_t* args,
                                  void* user_data);

typedef struct cdsl_action_cb_entry {
    char* action_name;
    cdsl_action_cb_t cb;
    struct cdsl_action_cb_entry* next;
} cdsl_action_cb_entry_t;

typedef struct cdsl_vm {
    const cdsl_schema_t* schema;
    cdsl_action_cb_entry_t* callbacks;
    void* user_data;
} cdsl_vm_t;

typedef enum {
    CDSL_STATUS_PASSED,
    CDSL_STATUS_PARTIALLY_PASSED,
    CDSL_STATUS_FAILED,
    CDSL_STATUS_ERROR
} cdsl_rule_status_t;

typedef struct {
    char* metric_name;
    char* description;
    int max_weight;
    int score_obtained;
    int is_critical;
    int is_passed;
    char* matched_case_expr;
    char* violation_reason;
} cdsl_metric_result_t;

typedef struct {
    char* rule_name;
    char* description;
    cdsl_metric_result_t* metrics;
    int metric_count;
    int total_max_score;
    int total_obtained_score;
    cdsl_rule_status_t status;
    char* decision_summary;
} cdsl_rule_report_t;

cdsl_context_t* cdsl_context_create(const cdsl_schema_t* schema);
void cdsl_context_free(cdsl_context_t* ctx);
void cdsl_context_set_int(cdsl_context_t* ctx, const char* name, int val);
void cdsl_context_set_float(cdsl_context_t* ctx, const char* name, double val);
void cdsl_context_set_bool(cdsl_context_t* ctx, const char* name, int val);
void cdsl_context_set_string(cdsl_context_t* ctx, const char* name, const char* val);
int cdsl_context_load_json(cdsl_context_t* ctx, const char* json_str);

cdsl_vm_t* cdsl_vm_create(const cdsl_schema_t* schema);
void cdsl_vm_free(cdsl_vm_t* vm);
void cdsl_vm_register_action(cdsl_vm_t* vm, const char* action_name, cdsl_action_cb_t cb);

cdsl_rule_report_t* cdsl_vm_execute(cdsl_vm_t* vm, const cdsl_rule_t* rule, cdsl_context_t* ctx);
void cdsl_report_free(cdsl_rule_report_t* report);
void cdsl_report_print(const cdsl_rule_report_t* report);

#endif
