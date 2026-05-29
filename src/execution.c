#include "execution.h"
#include "cdsl_json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

cdsl_context_t* cdsl_context_create(const cdsl_schema_t* schema) {
    cdsl_context_t* ctx = calloc(1, sizeof(*ctx));
    ctx->schema = schema;
    return ctx;
}

void cdsl_context_free(cdsl_context_t* ctx) {
    if (!ctx) return;
    cdsl_context_entry_t* e = ctx->entries;
    while (e) {
        cdsl_context_entry_t* next = e->next;
        free(e->name);
        if (e->value.type == CDSL_TYPE_STRING) free(e->value.data.string_val);
        free(e);
        e = next;
    }
    free(ctx);
}

static void ctx_set(cdsl_context_t* ctx, const char* name, cdsl_type_t type) {
    for (cdsl_context_entry_t* e = ctx->entries; e; e = e->next) {
        if (strcmp(e->name, name) == 0) {
            if (e->value.type == CDSL_TYPE_STRING) free(e->value.data.string_val);
            e->value.type = type;
            return;
        }
    }
    cdsl_context_entry_t* e = calloc(1, sizeof(*e));
    e->name = strdup(name);
    e->value.type = type;
    e->next = ctx->entries;
    ctx->entries = e;
}

static cdsl_context_entry_t* ctx_get(cdsl_context_t* ctx, const char* name) {
    for (cdsl_context_entry_t* e = ctx->entries; e; e = e->next) {
        if (strcmp(e->name, name) == 0) return e;
    }
    return NULL;
}

void cdsl_context_set_int(cdsl_context_t* ctx, const char* name, int val) {
    cdsl_context_entry_t* e = ctx_get(ctx, name);
    if (e) {
        if (e->value.type == CDSL_TYPE_STRING) free(e->value.data.string_val);
        e->value.type = CDSL_TYPE_INT;
        e->value.data.int_val = val;
    } else {
        cdsl_context_entry_t* ne = calloc(1, sizeof(*ne));
        ne->name = strdup(name);
        ne->value.type = CDSL_TYPE_INT;
        ne->value.data.int_val = val;
        ne->next = ctx->entries;
        ctx->entries = ne;
    }
}

void cdsl_context_set_float(cdsl_context_t* ctx, const char* name, double val) {
    cdsl_context_entry_t* e = ctx_get(ctx, name);
    if (e) {
        if (e->value.type == CDSL_TYPE_STRING) free(e->value.data.string_val);
        e->value.type = CDSL_TYPE_FLOAT;
        e->value.data.float_val = val;
    } else {
        cdsl_context_entry_t* ne = calloc(1, sizeof(*ne));
        ne->name = strdup(name);
        ne->value.type = CDSL_TYPE_FLOAT;
        ne->value.data.float_val = val;
        ne->next = ctx->entries;
        ctx->entries = ne;
    }
}

void cdsl_context_set_bool(cdsl_context_t* ctx, const char* name, int val) {
    cdsl_context_entry_t* e = ctx_get(ctx, name);
    if (e) {
        if (e->value.type == CDSL_TYPE_STRING) free(e->value.data.string_val);
        e->value.type = CDSL_TYPE_BOOL;
        e->value.data.bool_val = val;
    } else {
        cdsl_context_entry_t* ne = calloc(1, sizeof(*ne));
        ne->name = strdup(name);
        ne->value.type = CDSL_TYPE_BOOL;
        ne->value.data.bool_val = val;
        ne->next = ctx->entries;
        ctx->entries = ne;
    }
}

void cdsl_context_set_string(cdsl_context_t* ctx, const char* name, const char* val) {
    cdsl_context_entry_t* e = ctx_get(ctx, name);
    if (e) {
        if (e->value.type == CDSL_TYPE_STRING) free(e->value.data.string_val);
        e->value.type = CDSL_TYPE_STRING;
        e->value.data.string_val = strdup(val);
    } else {
        cdsl_context_entry_t* ne = calloc(1, sizeof(*ne));
        ne->name = strdup(name);
        ne->value.type = CDSL_TYPE_STRING;
        ne->value.data.string_val = strdup(val);
        ne->next = ctx->entries;
        ctx->entries = ne;
    }
}

static void load_json_recursive(cdsl_context_t* ctx, cdsl_json_value_t* obj, const char* prefix) {
    if (!obj || obj->type != JSON_OBJECT) return;
    cdsl_json_value_t* child = obj->value.object.items;
    while (child) {
        char key[256];
        if (prefix[0]) {
            snprintf(key, sizeof(key), "%s.%s", prefix, child->key);
        } else {
            snprintf(key, sizeof(key), "%s", child->key);
        }

        if (child->type == JSON_OBJECT) {
            load_json_recursive(ctx, child, key);
        } else if (child->type == JSON_NUMBER) {
            double v = child->value.number_val;
            if (v != (double)(int)v) {
                cdsl_context_set_float(ctx, key, v);
            } else {
                cdsl_context_set_int(ctx, key, (int)v);
            }
        } else if (child->type == JSON_BOOL) {
            cdsl_context_set_bool(ctx, key, child->value.bool_val);
        } else if (child->type == JSON_STRING) {
            cdsl_context_set_string(ctx, key, child->value.string_val);
        }
        child = child->next;
    }
}

int cdsl_context_load_json(cdsl_context_t* ctx, const char* json_str) {
    cdsl_json_value_t* root = cdsl_json_parse(json_str);
    if (!root) return 0;
    load_json_recursive(ctx, root, "");
    cdsl_json_free(root);
    return 1;
}

cdsl_vm_t* cdsl_vm_create(const cdsl_schema_t* schema) {
    cdsl_vm_t* vm = calloc(1, sizeof(*vm));
    vm->schema = schema;
    return vm;
}

void cdsl_vm_free(cdsl_vm_t* vm) {
    if (!vm) return;
    cdsl_action_cb_entry_t* cb = vm->callbacks;
    while (cb) {
        cdsl_action_cb_entry_t* next = cb->next;
        free(cb->action_name);
        free(cb);
        cb = next;
    }
    free(vm);
}

void cdsl_vm_register_action(cdsl_vm_t* vm, const char* action_name, cdsl_action_cb_t cb) {
    cdsl_action_cb_entry_t* e = calloc(1, sizeof(*e));
    e->action_name = strdup(action_name);
    e->cb = cb;
    e->next = vm->callbacks;
    vm->callbacks = e;
}

static cdsl_value_t eval_expr(cdsl_expr_node_t* expr, cdsl_context_t* ctx) {
    cdsl_value_t result = { .type = CDSL_TYPE_VOID };
    if (!expr) return result;

    switch (expr->type) {
        case CDSL_EXPR_INT:
            result.type = CDSL_TYPE_INT;
            result.data.int_val = expr->data.int_val;
            break;
        case CDSL_EXPR_FLOAT:
            result.type = CDSL_TYPE_FLOAT;
            result.data.float_val = expr->data.float_val;
            break;
        case CDSL_EXPR_BOOL:
            result.type = CDSL_TYPE_BOOL;
            result.data.bool_val = expr->data.bool_val;
            break;
        case CDSL_EXPR_STRING:
            result.type = CDSL_TYPE_STRING;
            result.data.string_val = expr->data.string_val;
            break;
        case CDSL_EXPR_ID: {
            cdsl_context_entry_t* e = ctx_get(ctx, expr->data.id_val);
            if (e) return e->value;
            break;
        }
        case CDSL_EXPR_UNARY: {
            cdsl_value_t v = eval_expr(expr->data.unary.expr, ctx);
            if (expr->data.unary.op == CDSL_OP_NOT) {
                result.type = CDSL_TYPE_BOOL;
                result.data.bool_val = !v.data.bool_val;
            }
            break;
        }
        case CDSL_EXPR_BINARY: {
            cdsl_op_t op = expr->data.binary.op;
            if (op == CDSL_OP_AND) {
                cdsl_value_t l = eval_expr(expr->data.binary.left, ctx);
                if (!l.data.bool_val) {
                    result.type = CDSL_TYPE_BOOL;
                    result.data.bool_val = 0;
                    break;
                }
                cdsl_value_t r = eval_expr(expr->data.binary.right, ctx);
                result.type = CDSL_TYPE_BOOL;
                result.data.bool_val = r.data.bool_val;
                break;
            }
            if (op == CDSL_OP_OR) {
                cdsl_value_t l = eval_expr(expr->data.binary.left, ctx);
                if (l.data.bool_val) {
                    result.type = CDSL_TYPE_BOOL;
                    result.data.bool_val = 1;
                    break;
                }
                cdsl_value_t r = eval_expr(expr->data.binary.right, ctx);
                result.type = CDSL_TYPE_BOOL;
                result.data.bool_val = r.data.bool_val;
                break;
            }
            cdsl_value_t l = eval_expr(expr->data.binary.left, ctx);
            cdsl_value_t r = eval_expr(expr->data.binary.right, ctx);

            if (l.type == CDSL_TYPE_STRING && r.type == CDSL_TYPE_STRING &&
                (op == CDSL_OP_EQ || op == CDSL_OP_NE)) {
                result.type = CDSL_TYPE_BOOL;
                int cmp = strcmp(l.data.string_val ? l.data.string_val : "",
                                 r.data.string_val ? r.data.string_val : "");
                result.data.bool_val = (op == CDSL_OP_EQ) ? (cmp == 0) : (cmp != 0);
                break;
            }

            double lv, rv;
            if (l.type == CDSL_TYPE_INT) lv = l.data.int_val;
            else if (l.type == CDSL_TYPE_FLOAT) lv = l.data.float_val;
            else if (l.type == CDSL_TYPE_BOOL) lv = l.data.bool_val;
            else { result.type = CDSL_TYPE_BOOL; result.data.bool_val = 0; break; }

            if (r.type == CDSL_TYPE_INT) rv = r.data.int_val;
            else if (r.type == CDSL_TYPE_FLOAT) rv = r.data.float_val;
            else if (r.type == CDSL_TYPE_BOOL) rv = r.data.bool_val;
            else { result.type = CDSL_TYPE_BOOL; result.data.bool_val = 0; break; }

            result.type = CDSL_TYPE_BOOL;
            switch (op) {
                case CDSL_OP_EQ: result.data.bool_val = fabs(lv - rv) < 1e-9; break;
                case CDSL_OP_NE: result.data.bool_val = fabs(lv - rv) >= 1e-9; break;
                case CDSL_OP_LT: result.data.bool_val = lv < rv; break;
                case CDSL_OP_GT: result.data.bool_val = lv > rv; break;
                case CDSL_OP_LE: result.data.bool_val = lv <= rv; break;
                case CDSL_OP_GE: result.data.bool_val = lv >= rv; break;
                default: break;
            }
            break;
        }
    }
    return result;
}

static void trigger_action(cdsl_vm_t* vm, cdsl_action_node_t* action) {
    if (!action) return;
    for (cdsl_action_cb_entry_t* cb = vm->callbacks; cb; cb = cb->next) {
        if (strcmp(cb->action_name, action->action_name) == 0) {
            cb->cb(action->action_name, action->args, vm->user_data);
            return;
        }
    }
}

static char* get_metric_meta(cdsl_meta_item_t* meta, const char* key, const char* def) {
    char* v = cdsl_meta_get(meta, key);
    return v ? v : (char*)def;
}

static cdsl_rule_report_t* execute_metric_rule(cdsl_vm_t* vm, const cdsl_rule_t* rule, cdsl_context_t* ctx) {
    cdsl_rule_report_t* report = calloc(1, sizeof(*report));
    report->rule_name = strdup(rule->name);
    char* desc = cdsl_meta_get(rule->meta_list, "description");
    report->description = desc ? strdup(desc) : strdup("");

    int metric_count = 0;
    for (cdsl_metric_node_t* m = rule->metrics; m; m = m->next) metric_count++;
    report->metric_count = metric_count;
    report->metrics = calloc(metric_count, sizeof(cdsl_metric_result_t));

    int total_max = 0, total_obtained = 0;
    int any_critical_failed = 0;

    int idx = 0;
    for (cdsl_metric_node_t* m = rule->metrics; m; m = m->next, idx++) {
        cdsl_metric_result_t* mr = &report->metrics[idx];
        mr->metric_name = strdup(m->name);
        char* mdesc = cdsl_meta_get(m->meta_list, "description");
        mr->description = mdesc ? strdup(mdesc) : strdup("");
        mr->max_weight = atoi(get_metric_meta(m->meta_list, "weight", "0"));
        mr->is_critical = (strcmp(get_metric_meta(m->meta_list, "is_critical", "false"), "true") == 0);

        total_max += mr->max_weight;

        int matched = 0;
        for (cdsl_case_node_t* c = m->case_list; c; c = c->next) {
            cdsl_value_t cond = eval_expr(c->condition, ctx);
            if (cond.type == CDSL_TYPE_BOOL && cond.data.bool_val) {
                trigger_action(vm, c->action);
                if (c->action && strcmp(c->action->action_name, "score") == 0 && c->action->args) {
                    cdsl_value_t sv = eval_expr(c->action->args->expr, ctx);
                    mr->score_obtained = (sv.type == CDSL_TYPE_INT) ? sv.data.int_val : 0;
                } else {
                    mr->score_obtained = mr->max_weight;
                }
                mr->is_passed = (mr->score_obtained > 0);
                matched = 1;
                break;
            }
        }

        if (!matched) {
            trigger_action(vm, m->default_action);
            if (m->default_action && strcmp(m->default_action->action_name, "score") == 0 && m->default_action->args) {
                cdsl_value_t sv = eval_expr(m->default_action->args->expr, ctx);
                mr->score_obtained = (sv.type == CDSL_TYPE_INT) ? sv.data.int_val : 0;
            } else if (m->default_action && strcmp(m->default_action->action_name, "fail_metric") == 0) {
                mr->score_obtained = 0;
                if (m->default_action->args && m->default_action->args->next) {
                    cdsl_value_t rv = eval_expr(m->default_action->args->next->expr, ctx);
                    if (rv.type == CDSL_TYPE_STRING) mr->violation_reason = strdup(rv.data.string_val);
                }
            } else {
                mr->score_obtained = 0;
            }
            mr->is_passed = (mr->score_obtained > 0);
            if (mr->violation_reason == NULL && !mr->is_passed) {
                mr->violation_reason = strdup("default_condition_triggered");
            }
        }

        if (mr->is_critical && !mr->is_passed) any_critical_failed = 1;
        total_obtained += mr->score_obtained;
    }

    report->total_max_score = total_max;
    report->total_obtained_score = total_obtained;

    if (any_critical_failed) {
        report->status = CDSL_STATUS_FAILED;
    } else {
        char* pass_str = cdsl_meta_get(rule->meta_list, "pass_threshold");
        char* partial_str = cdsl_meta_get(rule->meta_list, "partial_threshold");
        int pass_thresh = pass_str ? atoi(pass_str) : 80;
        int partial_thresh = partial_str ? atoi(partial_str) : 60;

        if (total_obtained >= pass_thresh) {
            report->status = CDSL_STATUS_PASSED;
        } else if (total_obtained >= partial_thresh) {
            report->status = CDSL_STATUS_PARTIALLY_PASSED;
        } else {
            report->status = CDSL_STATUS_FAILED;
        }
    }

    char buf[512];
    switch (report->status) {
        case CDSL_STATUS_PASSED:
            snprintf(buf, sizeof(buf), "PASSED (score: %d/%d)",
                     total_obtained, total_max);
            break;
        case CDSL_STATUS_PARTIALLY_PASSED:
            snprintf(buf, sizeof(buf), "PARTIALLY PASSED (score: %d/%d, needs improvement)",
                     total_obtained, total_max);
            break;
        case CDSL_STATUS_FAILED:
            snprintf(buf, sizeof(buf), "FAILED (score: %d/%d, critical items or threshold not met)",
                     total_obtained, total_max);
            break;
        default:
            snprintf(buf, sizeof(buf), "ERROR");
            break;
    }
    report->decision_summary = strdup(buf);

    return report;
}

static cdsl_rule_report_t* execute_simple_rule(cdsl_vm_t* vm, const cdsl_rule_t* rule, cdsl_context_t* ctx) {
    cdsl_rule_report_t* report = calloc(1, sizeof(*report));
    report->rule_name = strdup(rule->name);
    char* desc = cdsl_meta_get(rule->meta_list, "description");
    report->description = desc ? strdup(desc) : strdup("");
    report->metric_count = 1;
    report->metrics = calloc(1, sizeof(cdsl_metric_result_t));
    report->metrics[0].metric_name = strdup(rule->name);
    report->metrics[0].description = strdup(report->description);

    cdsl_value_t cond = eval_expr(rule->when_expr, ctx);
    int triggered = (cond.type == CDSL_TYPE_BOOL) ? cond.data.bool_val : 0;

    if (triggered) {
        report->metrics[0].score_obtained = 0;
        report->metrics[0].max_weight = 100;
        report->metrics[0].is_passed = 0;
        report->status = CDSL_STATUS_FAILED;
        report->total_max_score = 100;
        report->total_obtained_score = 0;
        trigger_action(vm, rule->then_action);
    } else {
        report->metrics[0].score_obtained = 100;
        report->metrics[0].max_weight = 100;
        report->metrics[0].is_passed = 1;
        report->status = CDSL_STATUS_PASSED;
        report->total_max_score = 100;
        report->total_obtained_score = 100;
    }

    char buf[256];
    snprintf(buf, sizeof(buf), "%s (score: %d/100)",
             triggered ? "FAILED" : "PASSED", report->total_obtained_score);
    report->decision_summary = strdup(buf);
    return report;
}

cdsl_rule_report_t* cdsl_vm_execute(cdsl_vm_t* vm, const cdsl_rule_t* rule, cdsl_context_t* ctx) {
    if (!vm || !rule || !ctx) return NULL;
    if (rule->metrics) return execute_metric_rule(vm, rule, ctx);
    return execute_simple_rule(vm, rule, ctx);
}

void cdsl_report_free(cdsl_rule_report_t* report) {
    if (!report) return;
    free(report->rule_name);
    free(report->description);
    free(report->decision_summary);
    for (int i = 0; i < report->metric_count; i++) {
        cdsl_metric_result_t* m = &report->metrics[i];
        free(m->metric_name);
        free(m->description);
        free(m->matched_case_expr);
        free(m->violation_reason);
    }
    free(report->metrics);
    free(report);
}

static const char* status_str(cdsl_rule_status_t s) {
    switch (s) {
        case CDSL_STATUS_PASSED:           return "PASSED";
        case CDSL_STATUS_PARTIALLY_PASSED: return "PARTIALLY PASSED";
        case CDSL_STATUS_FAILED:           return "FAILED";
        default:                           return "ERROR";
    }
}

void cdsl_report_print(const cdsl_rule_report_t* report) {
    if (!report) { printf("No report.\n"); return; }
    printf("\n========================================\n");
    printf("  AUDIT REPORT: %s\n", report->rule_name);
    printf("  %s\n", report->description);
    printf("========================================\n");

    for (int i = 0; i < report->metric_count; i++) {
        cdsl_metric_result_t* m = &report->metrics[i];
        printf("  [%s] %s (weight: %d, score: %d/%d)%s\n",
               m->is_passed ? "PASS" : "FAIL",
               m->metric_name,
               m->max_weight,
               m->score_obtained, m->max_weight,
               m->is_critical ? " *" : "");
        if (m->violation_reason) {
            printf("         Reason: %s\n", m->violation_reason);
        }
    }

    printf("----------------------------------------\n");
    printf("  Status:   %s\n", status_str(report->status));
    printf("  Score:    %d / %d\n", report->total_obtained_score, report->total_max_score);
    printf("  Summary:  %s\n", report->decision_summary);
    printf("========================================\n\n");
}
