#include "execution.h"
#include "cdsl_json.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <pthread.h>
#include <time.h>

static double get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e6 + ts.tv_nsec / 1e3;
}

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
    vm->debug_enabled = 0;
    return vm;
}

void cdsl_vm_set_debug(cdsl_vm_t* vm, int enabled) {
    if (vm) vm->debug_enabled = enabled;
}

cdsl_stats_t* cdsl_vm_get_stats(const cdsl_vm_t* vm) {
    if (!vm) return NULL;
    cdsl_stats_t* s = malloc(sizeof(cdsl_stats_t));
    *s = vm->stats;
    if (s->total_executions > 0) {
        s->avg_time_us = s->total_time_us / s->total_executions;
    }
    return s;
}

void cdsl_vm_reset_stats(cdsl_vm_t* vm) {
    if (vm) memset(&vm->stats, 0, sizeof(cdsl_stats_t));
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
    cdsl_func_entry_t* fn = vm->functions;
    while (fn) {
        cdsl_func_entry_t* next = fn->next;
        free(fn->func_name);
        free(fn);
        fn = next;
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

void cdsl_vm_register_function(cdsl_vm_t* vm, const char* func_name, cdsl_func_cb_t cb) {
    if (!vm || !func_name || !cb) return;
    cdsl_func_entry_t* e = calloc(1, sizeof(*e));
    e->func_name = strdup(func_name);
    e->cb = cb;
    e->next = vm->functions;
    vm->functions = e;
}

static cdsl_value_t eval_expr(cdsl_expr_node_t* expr, cdsl_context_t* ctx, cdsl_vm_t* vm, int debug) {
    cdsl_value_t result = { .type = CDSL_TYPE_VOID };
    if (!expr) return result;

    switch (expr->type) {
        case CDSL_EXPR_INT:
            result.type = CDSL_TYPE_INT;
            result.data.int_val = expr->data.int_val;
            if (debug) fprintf(stderr, "[TRACE]   literal int: %d\n", expr->data.int_val);
            break;
        case CDSL_EXPR_FLOAT:
            result.type = CDSL_TYPE_FLOAT;
            result.data.float_val = expr->data.float_val;
            if (debug) fprintf(stderr, "[TRACE]   literal float: %.2f\n", expr->data.float_val);
            break;
        case CDSL_EXPR_BOOL:
            result.type = CDSL_TYPE_BOOL;
            result.data.bool_val = expr->data.bool_val;
            if (debug) fprintf(stderr, "[TRACE]   literal bool: %s\n", expr->data.bool_val ? "true" : "false");
            break;
        case CDSL_EXPR_STRING:
            result.type = CDSL_TYPE_STRING;
            result.data.string_val = expr->data.string_val;
            if (debug) fprintf(stderr, "[TRACE]   literal string: \"%s\"\n", expr->data.string_val);
            break;
        case CDSL_EXPR_ID: {
            cdsl_context_entry_t* e = ctx_get(ctx, expr->data.id_val);
            if (e) {
                if (debug) {
                    fprintf(stderr, "[TRACE]   lookup: %s = ", expr->data.id_val);
                    switch (e->value.type) {
                        case CDSL_TYPE_INT:    fprintf(stderr, "%d\n", e->value.data.int_val); break;
                        case CDSL_TYPE_FLOAT:  fprintf(stderr, "%.2f\n", e->value.data.float_val); break;
                        case CDSL_TYPE_BOOL:   fprintf(stderr, "%s\n", e->value.data.bool_val ? "true" : "false"); break;
                        case CDSL_TYPE_STRING: fprintf(stderr, "\"%s\"\n", e->value.data.string_val); break;
                        default: fprintf(stderr, "?\n"); break;
                    }
                }
                return e->value;
            }
            if (debug) fprintf(stderr, "[TRACE]   lookup: %s = NOT FOUND\n", expr->data.id_val);
            break;
        }
        case CDSL_EXPR_UNARY: {
            cdsl_value_t v = eval_expr(expr->data.unary.expr, ctx, vm, debug);
            if (expr->data.unary.op == CDSL_OP_NOT) {
                result.type = CDSL_TYPE_BOOL;
                result.data.bool_val = !v.data.bool_val;
                if (debug) fprintf(stderr, "[TRACE]   NOT %s = %s\n",
                    v.data.bool_val ? "true" : "false",
                    result.data.bool_val ? "true" : "false");
            }
            break;
        }
        case CDSL_EXPR_BINARY: {
            cdsl_op_t op = expr->data.binary.op;
            if (op == CDSL_OP_AND) {
                cdsl_value_t l = eval_expr(expr->data.binary.left, ctx, vm, debug);
                if (!l.data.bool_val) {
                    result.type = CDSL_TYPE_BOOL;
                    result.data.bool_val = 0;
                    if (debug) fprintf(stderr, "[TRACE]   AND short-circuit: false\n");
                    break;
                }
                cdsl_value_t r = eval_expr(expr->data.binary.right, ctx, vm, debug);
                result.type = CDSL_TYPE_BOOL;
                result.data.bool_val = r.data.bool_val;
                if (debug) fprintf(stderr, "[TRACE]   AND: true AND %s = %s\n",
                    r.data.bool_val ? "true" : "false",
                    result.data.bool_val ? "true" : "false");
                break;
            }
            if (op == CDSL_OP_OR) {
                cdsl_value_t l = eval_expr(expr->data.binary.left, ctx, vm, debug);
                if (l.data.bool_val) {
                    result.type = CDSL_TYPE_BOOL;
                    result.data.bool_val = 1;
                    if (debug) fprintf(stderr, "[TRACE]   OR short-circuit: true\n");
                    break;
                }
                cdsl_value_t r = eval_expr(expr->data.binary.right, ctx, vm, debug);
                result.type = CDSL_TYPE_BOOL;
                result.data.bool_val = r.data.bool_val;
                if (debug) fprintf(stderr, "[TRACE]   OR: false OR %s = %s\n",
                    r.data.bool_val ? "true" : "false",
                    result.data.bool_val ? "true" : "false");
                break;
            }
            cdsl_value_t l = eval_expr(expr->data.binary.left, ctx, vm, debug);
            cdsl_value_t r = eval_expr(expr->data.binary.right, ctx, vm, debug);

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
        case CDSL_EXPR_CALL: {
            if (!vm) break;
            for (cdsl_func_entry_t* fn = vm->functions; fn; fn = fn->next) {
                if (strcmp(fn->func_name, expr->data.call.func_name) == 0) {
                    if (debug) fprintf(stderr, "[TRACE]   calling function: %s()\n", expr->data.call.func_name);
                    result = fn->cb(expr->data.call.func_name, expr->data.call.args, vm->user_data);
                    if (debug) {
                        fprintf(stderr, "[TRACE]   function result: ");
                        switch (result.type) {
                            case CDSL_TYPE_INT:    fprintf(stderr, "%d\n", result.data.int_val); break;
                            case CDSL_TYPE_FLOAT:  fprintf(stderr, "%.2f\n", result.data.float_val); break;
                            case CDSL_TYPE_BOOL:   fprintf(stderr, "%s\n", result.data.bool_val ? "true" : "false"); break;
                            case CDSL_TYPE_STRING: fprintf(stderr, "\"%s\"\n", result.data.string_val); break;
                            default: fprintf(stderr, "void\n"); break;
                        }
                    }
                    break;
                }
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
            if (vm->debug_enabled) fprintf(stderr, "[TRACE] eval CASE condition for metric '%s'\n", m->name);
            cdsl_value_t cond = eval_expr(c->condition, ctx, vm, vm->debug_enabled);
            if (cond.type == CDSL_TYPE_BOOL && cond.data.bool_val) {
                if (vm->debug_enabled) fprintf(stderr, "[TRACE]   CASE matched\n");
                trigger_action(vm, c->action);
                if (c->action && strcmp(c->action->action_name, "score") == 0 && c->action->args) {
                    cdsl_value_t sv = eval_expr(c->action->args->expr, ctx, vm, vm->debug_enabled);
                    mr->score_obtained = (sv.type == CDSL_TYPE_INT) ? sv.data.int_val : 0;
                } else {
                    mr->score_obtained = mr->max_weight;
                }
                if (vm->debug_enabled) fprintf(stderr, "[TRACE]   score obtained: %d/%d\n", mr->score_obtained, mr->max_weight);
                mr->is_passed = (mr->score_obtained > 0);
                matched = 1;
                break;
            }
        }

        if (!matched) {
            if (vm->debug_enabled) fprintf(stderr, "[TRACE]   no CASE matched, executing DEFAULT\n");
            trigger_action(vm, m->default_action);
            if (m->default_action && strcmp(m->default_action->action_name, "score") == 0 && m->default_action->args) {
                cdsl_value_t sv = eval_expr(m->default_action->args->expr, ctx, vm, vm->debug_enabled);
                mr->score_obtained = (sv.type == CDSL_TYPE_INT) ? sv.data.int_val : 0;
            } else if (m->default_action && strcmp(m->default_action->action_name, "fail_metric") == 0) {
                mr->score_obtained = 0;
                if (m->default_action->args && m->default_action->args->next) {
                    cdsl_value_t rv = eval_expr(m->default_action->args->next->expr, ctx, vm, vm->debug_enabled);
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

    if (vm->debug_enabled) fprintf(stderr, "[TRACE] Evaluating simple rule '%s'\n", rule->name);
    cdsl_value_t cond = eval_expr(rule->when_expr, ctx, vm, vm->debug_enabled);
    int triggered = (cond.type == CDSL_TYPE_BOOL) ? cond.data.bool_val : 0;
    if (vm->debug_enabled) fprintf(stderr, "[TRACE] WHEN result: %s\n", triggered ? "true" : "false");

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
    double t0 = get_time_us();
    cdsl_rule_report_t* rpt;
    if (rule->metrics) rpt = execute_metric_rule(vm, rule, ctx);
    else rpt = execute_simple_rule(vm, rule, ctx);
    double elapsed = get_time_us() - t0;
    vm->stats.total_executions++;
    vm->stats.total_rules_executed++;
    vm->stats.total_time_us += elapsed;
    if (rpt) vm->stats.total_metrics_evaluated += rpt->metric_count;
    return rpt;
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

char* cdsl_report_to_json(const cdsl_rule_report_t* report) {
    if (!report) return strdup("{}");
    char* json = malloc(4096);
    int off = 0;
    off += snprintf(json + off, 4096 - off,
        "{\"rule_name\":\"%s\",\"description\":\"%s\","
        "\"status\":\"%s\",\"total_max_score\":%d,\"total_obtained_score\":%d,"
        "\"decision_summary\":\"%s\",\"metrics\":[",
        report->rule_name ? report->rule_name : "",
        report->description ? report->description : "",
        status_str(report->status),
        report->total_max_score, report->total_obtained_score,
        report->decision_summary ? report->decision_summary : "");

    for (int i = 0; i < report->metric_count; i++) {
        cdsl_metric_result_t* m = &report->metrics[i];
        if (i > 0) off += snprintf(json + off, 4096 - off, ",");
        off += snprintf(json + off, 4096 - off,
            "{\"metric_name\":\"%s\",\"description\":\"%s\","
            "\"max_weight\":%d,\"score_obtained\":%d,\"is_critical\":%d,\"is_passed\":%d",
            m->metric_name ? m->metric_name : "",
            m->description ? m->description : "",
            m->max_weight, m->score_obtained, m->is_critical, m->is_passed);
        if (m->violation_reason) {
            off += snprintf(json + off, 4096 - off,
                ",\"violation_reason\":\"%s\"", m->violation_reason);
        }
        off += snprintf(json + off, 4096 - off, "}");
    }
    off += snprintf(json + off, 4096 - off, "]}");
    return json;
}

static unsigned int hash_dsl_string(const char* s) {
    unsigned int h = 5381;
    for (; *s; s++) h = ((h << 5) + h) + (unsigned char)*s;
    return h;
}

cdsl_compile_cache_t* cdsl_compile_cache_create(int capacity) {
    cdsl_compile_cache_t* c = calloc(1, sizeof(*c));
    c->capacity = capacity > 0 ? capacity : 64;
    c->entries = calloc(c->capacity, sizeof(cdsl_compiled_rule_t*));
    return c;
}

void cdsl_compile_cache_free(cdsl_compile_cache_t* cache) {
    if (!cache) return;
    for (int i = 0; i < cache->capacity; i++) {
        cdsl_compiled_rule_t* e = cache->entries[i];
        if (e) {
            free(e->dsl_hash);
            free(e);
        }
    }
    free(cache->entries);
    free(cache);
}

cdsl_compiled_rule_t* cdsl_compile(cdsl_compile_cache_t* cache, const char* dsl_code,
                                    const cdsl_schema_t* schema, char* err_buf, int err_buf_sz) {
    if (!cache || !dsl_code) {
        if (err_buf) snprintf(err_buf, err_buf_sz, "NULL cache or dsl_code");
        return NULL;
    }
    unsigned int idx = hash_dsl_string(dsl_code) % cache->capacity;
    cdsl_compiled_rule_t* existing = cache->entries[idx];
    if (existing && existing->dsl_hash && strcmp(existing->dsl_hash, dsl_code) == 0) {
        return existing;
    }
    cdsl_rule_t* rule = cdsl_parse_string(dsl_code);
    if (!rule) {
        if (err_buf) snprintf(err_buf, err_buf_sz, "Parse error");
        return NULL;
    }
    if (schema) {
        char verr[512] = {0};
        if (!cdsl_verify_rule(rule, schema, verr, sizeof(verr))) {
            if (err_buf) snprintf(err_buf, err_buf_sz, "Verify failed: %s", verr);
            cdsl_free_rule(rule);
            return 0;
        }
    }
    if (existing) {
        cdsl_free_rule(existing->rule);
        free(existing->dsl_hash);
    } else {
        existing = calloc(1, sizeof(*existing));
        cache->entries[idx] = existing;
    }
    existing->rule = rule;
    existing->dsl_hash = strdup(dsl_code);
    existing->verified = 1;
    return existing;
}

cdsl_rule_report_t* cdsl_vm_execute_compiled(cdsl_vm_t* vm, cdsl_compiled_rule_t* compiled,
                                              cdsl_context_t* ctx) {
    if (!vm || !compiled || !compiled->rule || !ctx) return NULL;
    return cdsl_vm_execute(vm, compiled->rule, ctx);
}

static void codegen_expr(FILE* f, cdsl_expr_node_t* expr, const char* indent) {
    if (!expr) return;
    switch (expr->type) {
        case CDSL_EXPR_INT:    fprintf(f, "%d", expr->data.int_val); break;
        case CDSL_EXPR_FLOAT:  fprintf(f, "%.2f", expr->data.float_val); break;
        case CDSL_EXPR_BOOL:   fprintf(f, "%s", expr->data.bool_val ? "1" : "0"); break;
        case CDSL_EXPR_STRING: fprintf(f, "\"%s\"", expr->data.string_val); break;
        case CDSL_EXPR_ID:     fprintf(f, "ctx_get_int(ctx, \"%s\")", expr->data.id_val); break;
        case CDSL_EXPR_BINARY: {
            const char* op_str = "?";
            switch (expr->data.binary.op) {
                case CDSL_OP_EQ: op_str = "=="; break;
                case CDSL_OP_NE: op_str = "!="; break;
                case CDSL_OP_LT: op_str = "<";  break;
                case CDSL_OP_GT: op_str = ">";  break;
                case CDSL_OP_LE: op_str = "<="; break;
                case CDSL_OP_GE: op_str = ">="; break;
                case CDSL_OP_AND: op_str = "&&"; break;
                case CDSL_OP_OR:  op_str = "||"; break;
                default: break;
            }
            fprintf(f, "(");
            codegen_expr(f, expr->data.binary.left, indent);
            fprintf(f, " %s ", op_str);
            codegen_expr(f, expr->data.binary.right, indent);
            fprintf(f, ")");
            break;
        }
        case CDSL_EXPR_UNARY:
            fprintf(f, "!");
            codegen_expr(f, expr->data.unary.expr, indent);
            break;
        case CDSL_EXPR_CALL:
            fprintf(f, "func_%s(ctx", expr->data.call.func_name);
            for (cdsl_arg_node_t* a = expr->data.call.args; a; a = a->next) {
                fprintf(f, ", ");
                codegen_expr(f, a->expr, indent);
            }
            fprintf(f, ")");
            break;
    }
}

static void codegen_action(FILE* f, cdsl_action_node_t* action, const char* indent) {
    if (!action) return;
    fprintf(f, "%saction_%s(", indent, action->action_name);
    int first = 1;
    for (cdsl_arg_node_t* a = action->args; a; a = a->next) {
        if (!first) fprintf(f, ", ");
        first = 0;
        codegen_expr(f, a->expr, indent);
    }
    fprintf(f, ");\n");
}

char* cdsl_codegen_rule_to_c(const cdsl_rule_t* rule, const cdsl_schema_t* schema) {
    if (!rule) return NULL;
    FILE* f = open_memstream(NULL, NULL);
    if (!f) return NULL;

    fprintf(f, "/* Auto-generated C code from DSL rule: %s */\n", rule->name);
    fprintf(f, "#include <stdio.h>\n#include <string.h>\n\n");

    if (rule->metrics) {
        fprintf(f, "int evaluate_%s(int (*get_int)(void* ctx, const char* name),\n", rule->name);
        fprintf(f, "                 void (*action)(const char* name, int score, const char* reason, void* ud),\n");
        fprintf(f, "                 void* ctx, void* ud) {\n");
        fprintf(f, "    int total = 0;\n");
        for (cdsl_metric_node_t* m = rule->metrics; m; m = m->next) {
            int weight = atoi(cdsl_meta_get(m->meta_list, "weight") ?: "0");
            int critical = (strcmp(cdsl_meta_get(m->meta_list, "is_critical") ?: "false", "true") == 0);
            fprintf(f, "    /* Metric: %s (weight=%d%s) */\n", m->name, weight, critical ? ", critical" : "");
            for (cdsl_case_node_t* c = m->case_list; c; c = c->next) {
                fprintf(f, "    if (");
                codegen_expr(f, c->condition, "        ");
                fprintf(f, ") {\n");
                if (c->action && strcmp(c->action->action_name, "score") == 0 && c->action->args) {
                    fprintf(f, "        total += ");
                    codegen_expr(f, c->action->args->expr, "            ");
                    fprintf(f, ";\n");
                }
                fprintf(f, "        goto next_metric_%s;\n", m->name);
                fprintf(f, "    }\n");
            }
            fprintf(f, "    /* DEFAULT */\n");
            if (m->default_action && strcmp(m->default_action->action_name, "fail_metric") == 0) {
                fprintf(f, "    action(\"fail_metric\", 0, \"default\", ud);\n");
                if (critical) {
                    fprintf(f, "    return -1; /* critical veto */\n");
                }
            }
            fprintf(f, "    next_metric_%s: ;\n", m->name);
        }
        fprintf(f, "    return total;\n");
        fprintf(f, "}\n");
    } else {
        fprintf(f, "int evaluate_%s(int (*get_int)(void* ctx, const char* name),\n", rule->name);
        fprintf(f, "                 void (*action)(const char* name, void* ud),\n");
        fprintf(f, "                 void* ctx, void* ud) {\n");
        fprintf(f, "    if (");
        codegen_expr(f, rule->when_expr, "        ");
        fprintf(f, ") {\n");
        codegen_action(f, rule->then_action, "        ");
        fprintf(f, "        return 0; /* FAILED */\n");
        fprintf(f, "    }\n");
        fprintf(f, "    return 1; /* PASSED */\n");
        fprintf(f, "}\n");
    }

    fflush(f);
    fclose(f);
    return NULL;
}

int cdsl_codegen_to_file(const cdsl_rule_t* rule, const cdsl_schema_t* schema, const char* filepath) {
    if (!rule || !filepath) return 0;
    char* code = cdsl_codegen_rule_to_c(rule, schema);
    if (!code) return 0;
    FILE* f = fopen(filepath, "w");
    if (!f) { free(code); return 0; }
    fputs(code, f);
    fclose(f);
    free(code);
    return 1;
}

cdsl_ruleset_t* cdsl_ruleset_create(void) {
    return calloc(1, sizeof(cdsl_ruleset_t));
}

void cdsl_ruleset_free(cdsl_ruleset_t* set) {
    if (!set) return;
    cdsl_ruleset_entry_t* e = set->entries;
    while (e) {
        cdsl_ruleset_entry_t* next = e->next;
        cdsl_free_rule(e->rule);
        free(e);
        e = next;
    }
    free(set);
}

void cdsl_ruleset_add(cdsl_ruleset_t* set, cdsl_rule_t* rule, int priority) {
    if (!set || !rule) return;
    cdsl_ruleset_entry_t* e = calloc(1, sizeof(*e));
    e->rule = rule;
    e->priority = priority;
    if (!set->entries || priority < set->entries->priority) {
        e->next = set->entries;
        set->entries = e;
    } else {
        cdsl_ruleset_entry_t* cur = set->entries;
        while (cur->next && cur->next->priority <= priority) cur = cur->next;
        e->next = cur->next;
        cur->next = e;
    }
    set->count++;
}

cdsl_ruleset_report_t* cdsl_vm_execute_ruleset(cdsl_vm_t* vm, cdsl_ruleset_t* set, cdsl_context_t* ctx) {
    if (!vm || !set || !ctx) return NULL;
    cdsl_ruleset_report_t* rpt = calloc(1, sizeof(*rpt));
    rpt->rule_count = set->count;
    rpt->rule_reports = calloc(set->count, sizeof(cdsl_rule_report_t*));

    int idx = 0;
    int agg_score = 0, agg_max = 0;
    for (cdsl_ruleset_entry_t* e = set->entries; e; e = e->next, idx++) {
        rpt->rule_reports[idx] = cdsl_vm_execute(vm, e->rule, ctx);
        cdsl_rule_report_t* rr = rpt->rule_reports[idx];
        if (!rr) continue;
        agg_score += rr->total_obtained_score;
        agg_max += rr->total_max_score;
        switch (rr->status) {
            case CDSL_STATUS_PASSED:           rpt->total_passed++; break;
            case CDSL_STATUS_PARTIALLY_PASSED: rpt->total_partially++; break;
            case CDSL_STATUS_FAILED:           rpt->total_failed++; break;
            default:                           rpt->total_error++; break;
        }
    }
    rpt->aggregate_score = agg_score;
    rpt->aggregate_max = agg_max;

    char buf[256];
    snprintf(buf, sizeof(buf), "%d rules: %d passed, %d partial, %d failed | Score: %d/%d",
             rpt->rule_count, rpt->total_passed, rpt->total_partially,
             rpt->total_failed, agg_score, agg_max);
    rpt->summary = strdup(buf);
    return rpt;
}

void cdsl_ruleset_report_free(cdsl_ruleset_report_t* report) {
    if (!report) return;
    if (report->rule_reports) {
        for (int i = 0; i < report->rule_count; i++) {
            cdsl_report_free(report->rule_reports[i]);
        }
        free(report->rule_reports);
    }
    free(report->summary);
    free(report);
}

void cdsl_ruleset_report_print(const cdsl_ruleset_report_t* report) {
    if (!report) { printf("No report.\n"); return; }
    printf("\n========================================\n");
    printf("  BATCH AUDIT REPORT\n");
    printf("========================================\n");
    for (int i = 0; i < report->rule_count; i++) {
        cdsl_rule_report_t* rr = report->rule_reports[i];
        if (rr) {
            printf("  [%s] %s - %s (score: %d/%d)\n",
                   status_str(rr->status), rr->rule_name, rr->decision_summary,
                   rr->total_obtained_score, rr->total_max_score);
        }
    }
    printf("----------------------------------------\n");
    printf("  Summary:  %s\n", report->summary);
    printf("========================================\n\n");
}

int cdsl_ruleset_remove(cdsl_ruleset_t* set, const char* rule_name) {
    if (!set || !rule_name) return 0;
    cdsl_ruleset_entry_t** pp = &set->entries;
    while (*pp) {
        if ((*pp)->rule && (*pp)->rule->name && strcmp((*pp)->rule->name, rule_name) == 0) {
            cdsl_ruleset_entry_t* del = *pp;
            *pp = del->next;
            cdsl_free_rule(del->rule);
            free(del);
            set->count--;
            return 1;
        }
        pp = &(*pp)->next;
    }
    return 0;
}

static char* read_file(const char* path) {
    FILE* f = fopen(path, "r");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    char* buf = malloc(sz + 1);
    if (buf) {
        fread(buf, 1, sz, f);
        buf[sz] = '\0';
    }
    fclose(f);
    return buf;
}

int cdsl_ruleset_load_string(cdsl_ruleset_t* set, const char* dsl_code, int priority,
                              const cdsl_schema_t* schema, char* err_buf, int err_buf_sz) {
    if (!set || !dsl_code) {
        if (err_buf) snprintf(err_buf, err_buf_sz, "NULL set or dsl_code");
        return 0;
    }
    cdsl_rule_t* rule = cdsl_parse_string(dsl_code);
    if (!rule) {
        if (err_buf) snprintf(err_buf, err_buf_sz, "Parse error");
        return 0;
    }
    if (schema) {
        char verr[512] = {0};
        if (!cdsl_verify_rule(rule, schema, verr, sizeof(verr))) {
            if (err_buf) snprintf(err_buf, err_buf_sz, "Verify failed: %s", verr);
            cdsl_free_rule(rule);
            return 0;
        }
    }
    cdsl_ruleset_add(set, rule, priority);
    return 1;
}

int cdsl_ruleset_load_file(cdsl_ruleset_t* set, const char* filepath, int priority,
                            const cdsl_schema_t* schema, char* err_buf, int err_buf_sz) {
    char* content = read_file(filepath);
    if (!content) {
        if (err_buf) snprintf(err_buf, err_buf_sz, "Cannot read file: %s", filepath);
        return 0;
    }
    int ok = cdsl_ruleset_load_string(set, content, priority, schema, err_buf, err_buf_sz);
    free(content);
    return ok;
}

int cdsl_ruleset_reload_file(cdsl_ruleset_t* set, const char* rule_name,
                              const char* filepath, const cdsl_schema_t* schema,
                              char* err_buf, int err_buf_sz) {
    cdsl_ruleset_remove(set, rule_name);
    return cdsl_ruleset_load_file(set, filepath, 0, schema, err_buf, err_buf_sz);
}

typedef struct {
    cdsl_vm_t* vm;
    cdsl_rule_t* rule;
    cdsl_context_t* ctx;
    cdsl_rule_report_t* result;
} parallel_thread_arg_t;

static void* parallel_worker(void* arg) {
    parallel_thread_arg_t* ta = (parallel_thread_arg_t*)arg;
    ta->result = cdsl_vm_execute(ta->vm, ta->rule, ta->ctx);
    return NULL;
}

cdsl_ruleset_report_t* cdsl_vm_execute_ruleset_parallel(cdsl_vm_t* vm, cdsl_ruleset_t* set,
                                                          cdsl_context_t* ctx, int thread_count) {
    if (!vm || !set || !ctx) return NULL;
    if (set->count == 0) return cdsl_vm_execute_ruleset(vm, set, ctx);
    if (thread_count <= 0) thread_count = 4;
    if (thread_count > set->count) thread_count = set->count;

    cdsl_ruleset_report_t* rpt = calloc(1, sizeof(*rpt));
    rpt->rule_count = set->count;
    rpt->rule_reports = calloc(set->count, sizeof(cdsl_rule_report_t*));

    int idx = 0;
    for (cdsl_ruleset_entry_t* e = set->entries; e; e = e->next) {
        int batch = (thread_count < set->count - idx) ? thread_count : set->count - idx;
        if (batch > thread_count) batch = thread_count;

        pthread_t* threads = malloc(sizeof(pthread_t) * batch);
        parallel_thread_arg_t* args = malloc(sizeof(parallel_thread_arg_t) * batch);

        cdsl_ruleset_entry_t* cur = e;
        for (int i = 0; i < batch && cur; i++, cur = cur->next) {
            args[i].vm = vm;
            args[i].rule = cur->rule;
            args[i].ctx = ctx;
            args[i].result = NULL;
            pthread_create(&threads[i], NULL, parallel_worker, &args[i]);
        }

        for (int i = 0; i < batch; i++) {
            pthread_join(threads[i], NULL);
            rpt->rule_reports[idx++] = args[i].result;
        }

        free(threads);
        free(args);
        e = cur ? cur : e;
    }

    int agg_score = 0, agg_max = 0;
    for (int i = 0; i < rpt->rule_count; i++) {
        cdsl_rule_report_t* rr = rpt->rule_reports[i];
        if (!rr) continue;
        agg_score += rr->total_obtained_score;
        agg_max += rr->total_max_score;
        switch (rr->status) {
            case CDSL_STATUS_PASSED:           rpt->total_passed++; break;
            case CDSL_STATUS_PARTIALLY_PASSED: rpt->total_partially++; break;
            case CDSL_STATUS_FAILED:           rpt->total_failed++; break;
            default:                           rpt->total_error++; break;
        }
    }
    rpt->aggregate_score = agg_score;
    rpt->aggregate_max = agg_max;

    char buf[256];
    snprintf(buf, sizeof(buf), "%d rules: %d passed, %d partial, %d failed | Score: %d/%d (parallel)",
             rpt->rule_count, rpt->total_passed, rpt->total_partially,
             rpt->total_failed, agg_score, agg_max);
    rpt->summary = strdup(buf);
    return rpt;
}

int cdsl_ruleset_validate_deps(const cdsl_ruleset_t* set, char* err_buf, int err_buf_sz) {
    if (!set) return 1;
    for (cdsl_ruleset_entry_t* e = set->entries; e; e = e->next) {
        if (!e->rule || !e->rule->meta_list) continue;
        char* deps = cdsl_meta_get(e->rule->meta_list, "depends_on");
        if (!deps) continue;
        char dep_buf[1024];
        strncpy(dep_buf, deps, sizeof(dep_buf) - 1);
        dep_buf[sizeof(dep_buf) - 1] = '\0';
        char* token = strtok(dep_buf, ",");
        while (token) {
            while (*token == ' ') token++;
            int found = 0;
            for (cdsl_ruleset_entry_t* f = set->entries; f; f = f->next) {
                if (f->rule && f->rule->name && strcmp(f->rule->name, token) == 0) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                if (err_buf) snprintf(err_buf, err_buf_sz,
                    "Rule '%s' depends on '%s' which is not in the ruleset",
                    e->rule->name, token);
                return 0;
            }
            token = strtok(NULL, ",");
        }
    }
    return 1;
}

int cdsl_ruleset_topo_sort(cdsl_ruleset_t* set) {
    if (!set || set->count <= 1) return 1;

    int n = set->count;
    cdsl_ruleset_entry_t** arr = malloc(sizeof(cdsl_ruleset_entry_t*) * n);
    int idx = 0;
    for (cdsl_ruleset_entry_t* e = set->entries; e; e = e->next) arr[idx++] = e;

    int* in_degree = calloc(n, sizeof(int));
    for (int i = 0; i < n; i++) {
        if (!arr[i]->rule || !arr[i]->rule->meta_list) continue;
        char* deps = cdsl_meta_get(arr[i]->rule->meta_list, "depends_on");
        if (!deps) continue;
        char dep_buf[1024];
        strncpy(dep_buf, deps, sizeof(dep_buf) - 1);
        dep_buf[sizeof(dep_buf) - 1] = '\0';
        char* token = strtok(dep_buf, ",");
        while (token) {
            while (*token == ' ') token++;
            for (int j = 0; j < n; j++) {
                if (arr[j]->rule && arr[j]->rule->name && strcmp(arr[j]->rule->name, token) == 0) {
                    in_degree[i]++;
                }
            }
            token = strtok(NULL, ",");
        }
    }

    int changed = 1;
    while (changed) {
        changed = 0;
        for (int i = 0; i < n; i++) {
            if (in_degree[i] == 0 && arr[i]->priority != -1) {
                arr[i]->priority = -1;
                for (int j = 0; j < n; j++) {
                    if (arr[j]->rule && arr[j]->rule->meta_list && arr[j]->priority != -1) {
                        char* deps = cdsl_meta_get(arr[j]->rule->meta_list, "depends_on");
                        if (deps && strstr(deps, arr[i]->rule->name)) {
                            in_degree[j]--;
                        }
                    }
                }
                changed = 1;
            }
        }
    }

    int new_p = 1;
    for (int i = 0; i < n; i++) {
        if (arr[i]->priority == -1) {
            arr[i]->priority = new_p++;
        }
    }

    set->entries = NULL;
    cdsl_ruleset_entry_t* tail = NULL;
    for (int p = 1; p <= n; p++) {
        for (int i = 0; i < n; i++) {
            if (arr[i]->priority == p) {
                arr[i]->next = NULL;
                if (!set->entries) {
                    set->entries = arr[i];
                    tail = arr[i];
                } else {
                    tail->next = arr[i];
                    tail = arr[i];
                }
            }
        }
    }

    free(in_degree);
    free(arr);
    return 1;
}
