# User Guide

## 1. Quick Start

### 1.1 Requirements

- C compiler (GCC / Clang)
- CMake 3.14+
- Flex 2.6+
- Bison 3.8+

### 1.2 Build

```bash
git clone <repo-url>
cd dsl
mkdir build && cd build
cmake ..
make -j$(nproc)
```

### 1.3 Run Demo

```bash
./cdsl_demo
```

6 demo scenarios:
1. **Supplier Qualification Audit** — AI-generated DSL, blacklist (critical) + capital + experience scoring
2. **Document Format Audit** — AI-generated DSL, format (critical) + signature + size scoring
3. **Content Safety Audit** — AI-generated DSL, sensitive words + PII + spam scoring
4. **JSON Context** — Variables loaded from JSON string
5. **Simple Rules** — Independent pass/fail checks (blacklist, capital floor, format)
6. **RuleSet Batch** — Priority-ordered multi-rule execution with aggregate report

### 1.4 Run Tests

```bash
ctest
# or directly:
./test_ast
./test_execution
```

### 1.5 Generate Documentation

```bash
make doc
# → docs/html/index.html
```

### 1.6 Integration

**Method 1: add_subdirectory**

```cmake
add_subdirectory(path/to/cdsl)
target_link_libraries(your_app PRIVATE cdsl)
```

**Method 2: Installed find_package**

```bash
cd build && cmake --install . --prefix /usr/local
```

```cmake
find_package(cdsl REQUIRED)
target_link_libraries(your_app PRIVATE cdsl::cdsl_static)
```

**Method 3: pkg-config**

```bash
pkg-config --cflags --libs cdsl
```

---

## 2. Core API Usage

### 2.1 Define Schema

Schema is the contract between rules and the host program — registers all available variables and actions.

```c
#include "abstract.h"

cdsl_schema_t* schema = cdsl_schema_create();

// Register variables
cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
cdsl_schema_register_var(schema, "user.name", CDSL_TYPE_STRING);
cdsl_schema_register_var(schema, "user.is_active", CDSL_TYPE_BOOL);

// Register actions
cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
cdsl_schema_register_action(schema, "score", CDSL_TYPE_VOID, 1, CDSL_TYPE_INT);
cdsl_schema_register_action(schema, "fail_metric", CDSL_TYPE_VOID, 2, CDSL_TYPE_INT, CDSL_TYPE_STRING);
```

### 2.2 Parse Rule

```c
#include "ast.h"

const char* dsl = 
    "RULE check_age {"
    "  META { description = \"Age check\" }"
    "  WHEN user.age >= 18"
    "  THEN block(\"adult\")"
    "}";

cdsl_rule_t* rule = cdsl_parse_string(dsl);
if (!rule) {
    fprintf(stderr, "Parse error\n");
    return;
}
```

### 2.3 Verify Rule

```c
#include "abstract.h"

// Simple verification
char err[512] = {0};
if (!cdsl_verify_rule(rule, schema, err, sizeof(err))) {
    fprintf(stderr, "Verification failed: %s\n", err);
}

// Detailed verification (collect all errors)
cdsl_error_list_t* errors = cdsl_verify_rule_detailed(rule, schema);
if (errors->count > 0) {
    cdsl_error_list_print(errors);
}
cdsl_error_list_free(errors);
```

### 2.4 Create VM and Register Actions

```c
#include "execution.h"

void on_block(const char* name, cdsl_arg_node_t* args, void* ud) {
    if (args && args->expr && args->expr->type == CDSL_EXPR_STRING) {
        printf("BLOCKED: %s\n", args->expr->data.string_val);
    }
}

cdsl_vm_t* vm = cdsl_vm_create(schema);
cdsl_vm_register_action(vm, "block", on_block);
cdsl_vm_register_action(vm, "score", on_score);
cdsl_vm_register_action(vm, "fail_metric", on_fail_metric);
```

### 2.5 Bind Context and Execute

```c
// Method 1: API binding
cdsl_context_t* ctx = cdsl_context_create(schema);
cdsl_context_set_int(ctx, "user.age", 25);
cdsl_context_set_string(ctx, "user.name", "Alice");
cdsl_context_set_bool(ctx, "user.is_active", 1);

// Method 2: JSON loading
cdsl_context_load_json(ctx, "{\"user\":{\"age\":25,\"name\":\"Alice\",\"is_active\":true}}");

// Execute
cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);

// View report
cdsl_report_print(rpt);

// Serialize to JSON
char* json = cdsl_report_to_json(rpt);
printf("%s\n", json);
free(json);

// Cleanup
cdsl_report_free(rpt);
cdsl_context_free(ctx);
cdsl_vm_free(vm);
cdsl_free_rule(rule);
cdsl_schema_free(schema);
```

---

## 3. Scoring Rules

### 3.1 Define Scoring Rule

```c
const char* dsl =
    "RULE scoring {"
    "  META { description = \"Test\" pass_threshold = \"80\" partial_threshold = \"50\" }"
    "  METRIC m1 {"
    "    META { description = \"Metric 1\" weight = \"60\" }"
    "    CASE user.age >= 18 THEN score(60)"
    "    DEFAULT score(0)"
    "  }"
    "  METRIC m2 {"
    "    META { description = \"Metric 2\" weight = \"40\" is_critical = \"true\" }"
    "    CASE user.is_active == true THEN score(40)"
    "    DEFAULT fail_metric(0, \"inactive\")"
    "  }"
    "}";
```

### 3.2 Tri-State Results

```c
cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);

switch (rpt->status) {
    case CDSL_STATUS_PASSED:
        printf("Passed: %d/%d\n", rpt->total_obtained_score, rpt->total_max_score);
        break;
    case CDSL_STATUS_PARTIALLY_PASSED:
        printf("Partially passed: %d/%d\n", rpt->total_obtained_score, rpt->total_max_score);
        break;
    case CDSL_STATUS_FAILED:
        printf("Failed: %d/%d\n", rpt->total_obtained_score, rpt->total_max_score);
        for (int i = 0; i < rpt->metric_count; i++) {
            if (!rpt->metrics[i].is_passed && rpt->metrics[i].violation_reason) {
                printf("  Failed item: %s - %s\n",
                       rpt->metrics[i].metric_name, rpt->metrics[i].violation_reason);
            }
        }
        break;
}
```

---

## 4. RuleSet Batch Execution

### 4.1 Basic Usage

```c
cdsl_ruleset_t* set = cdsl_ruleset_create();

// Add rules (lower priority = executes first)
cdsl_ruleset_add(set, rule_high_priority, 1);
cdsl_ruleset_add(set, rule_medium_priority, 5);
cdsl_ruleset_add(set, rule_low_priority, 10);

// Execute
cdsl_ruleset_report_t* batch = cdsl_vm_execute_ruleset(vm, set, ctx);
cdsl_ruleset_report_print(batch);

// View aggregate
printf("Passed: %d, Partial: %d, Failed: %d\n",
       batch->total_passed, batch->total_partially, batch->total_failed);
printf("Total: %d/%d\n", batch->aggregate_score, batch->aggregate_max);

cdsl_ruleset_report_free(batch);
cdsl_ruleset_free(set);
```

### 4.2 Hot Reload

```c
// Load from file
cdsl_ruleset_load_file(set, "rules.dsl", 1, schema, err_buf, sizeof(err_buf));

// Remove a rule by name
cdsl_ruleset_remove(set, "check_blacklist");

// Reload (re-parse and replace in-place)
cdsl_ruleset_reload_file(set, "check_blacklist", "rules.dsl", schema, err_buf, sizeof(err_buf));
```

### 4.3 Parallel Execution

```c
// Thread count = 0 uses hardware concurrency
cdsl_ruleset_report_t* batch = cdsl_vm_execute_ruleset_parallel(vm, set, ctx, 0);
```

### 4.4 Dependency Ordering

```c
// Define dependencies in META:
// RULE r1 { META { depends_on = "r2,r3" } ... }

// Validate and topologically sort
cdsl_ruleset_validate_deps(set, err_buf, sizeof(err_buf));
cdsl_ruleset_topo_sort(set);
```

---

## 5. Debug Trace Mode

Enable trace output to stderr for expression evaluation debugging:

```c
cdsl_vm_t* vm = cdsl_vm_create(schema);
cdsl_vm_set_debug(vm, 1);  // Enable trace

cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);

// Sample trace output:
// [eval] expr: binary(>=) left: id(user.age) right: int(18)
// [eval]   result: bool(1)
// [action] block("adult") triggered
```

---

## 6. Performance Monitoring

```c
// After execution(s):
cdsl_stats_t* stats = cdsl_vm_get_stats(vm);
printf("Total executions: %ld\n", stats->total_executions);
printf("Total rules:      %ld\n", stats->total_rules_executed);
printf("Total metrics:    %ld\n", stats->total_metrics_evaluated);
printf("Total actions:    %ld\n", stats->total_actions_triggered);
printf("Total time:       %.0f us\n", stats->total_time_us);
printf("Avg time/rule:    %.2f us\n", stats->avg_time_us);

// Reset for a new measurement period
cdsl_vm_reset_stats(vm);
```

---

## 7. Compilation Cache

Avoid re-parsing and re-verifying the same DSL string:

```c
cdsl_compile_cache_t* cache = cdsl_compile_cache_create(64);

// First call: parse + verify + cache
cdsl_compiled_rule_t* cr = cdsl_compile(cache, dsl_string, schema, err_buf, sizeof(err_buf));
if (!cr) {
    fprintf(stderr, "Compile failed: %s\n", err_buf);
    return;
}

// Execute from cache (no re-parse)
cdsl_rule_report_t* rpt = cdsl_vm_execute_compiled(vm, cr, ctx);

// Subsequent calls with same DSL string hit the cache

cdsl_compile_cache_free(cache);
```

---

## 8. Custom Functions

Register C callbacks as expression functions usable in DSL:

```c
// Callback
cdsl_value_t my_strlen(const char* name, cdsl_arg_node_t* args, void* ud) {
    cdsl_value_t v = { .type = CDSL_TYPE_INT, .data.int_val = 0 };
    if (args && args->expr && args->expr->type == CDSL_EXPR_STRING) {
        v.data.int_val = strlen(args->expr->data.string_val);
    }
    return v;
}

cdsl_value_t my_abs(const char* name, cdsl_arg_node_t* args, void* ud) {
    cdsl_value_t v = { .type = CDSL_TYPE_INT, .data.int_val = 0 };
    if (args && args->expr && args->expr->type == CDSL_EXPR_INT) {
        v.data.int_val = abs(args->expr->data.int_val);
    }
    return v;
}

// Register
cdsl_vm_register_function(vm, "strlen", my_strlen);
cdsl_vm_register_function(vm, "abs", my_abs);
```

Usage in DSL:
```dsl
RULE check_name {
    META { description = "Name validation" }
    WHEN strlen(user.name) > 0
    THEN record_warning("name_exists")
}
```

---

## 9. Template & Inheritance

Define reusable metric blocks with `TEMPLATE` and inherit with `EXTENDS`:

```dsl
TEMPLATE base_audit {
    METRIC blacklist_check {
        META { weight = "30" is_critical = "true" }
        CASE supplier.is_blacklisted == false THEN score(30)
        DEFAULT fail_metric(0, "blacklisted")
    }
}

RULE full_audit EXTENDS base_audit {
    METRIC capital_check {
        META { description = "Capital evaluation" weight = "40" }
        CASE supplier.registered_capital >= 5000000 THEN score(40)
        DEFAULT score(0)
    }
    METRIC experience_check {
        META { description = "Experience check" weight = "30" }
        CASE supplier.years_in_business >= 3 THEN score(30)
        DEFAULT score(0)
    }
}
```

Template metrics are copied into the extending rule before custom metrics.

---

## 10. Code Generation (DSL → C)

Generate equivalent C source code from a rule:

```c
char* c_code = cdsl_codegen_rule_to_c(rule, schema);
printf("%s\n", c_code);
free(c_code);

// Write directly to file
cdsl_codegen_to_file(rule, schema, "generated_rule.c");
```

---

## 11. Visualization (Graphviz DOT)

Generate DOT format graphs for visualization:

```c
// Single rule
char* dot = cdsl_rule_to_dot(rule);
cdsl_rule_to_dot_file(rule, "rule.dot");

// Ruleset with dependency arrows
char* set_dot = cdsl_ruleset_to_dot(ruleset);
cdsl_ruleset_to_dot_file(ruleset, "ruleset.dot");
```

Render with Graphviz:
```bash
dot -Tpng rule.dot -o rule.png
dot -Tpng ruleset.dot -o ruleset.png
```

---

## 12. AI Integration

### 12.1 Offline Mode (Demo)

```c
#include "ai_bridge.h"

cdsl_ai_config_t cfg = cdsl_ai_config_default();  // use_mock = 1

// Natural language to DSL — generates schema-aware rules
char* dsl = cdsl_ai_translate("supplier qualification audit", schema, &cfg);
printf("Generated DSL:\n%s\n", dsl);

// Optional business context to guide generation
cfg.business_context = "Evaluate supplier capital, blacklist status, and experience";
char* dsl2 = cdsl_ai_translate("supplier audit", schema, &cfg);

// Rule safety review
cdsl_ai_review_t* review = cdsl_ai_review(dsl, schema, &cfg);
printf("Approved: %s, Risk: %d/100\n", review->approved ? "YES" : "NO", review->risk_score);

cdsl_ai_review_free(review);
free(dsl);
```

The offline mode generates DSL using the registered schema variables — one metric per variable — with appropriate CASE conditions based on each variable's type.

### 12.2 API Mode (Real LLM)

```c
cdsl_ai_config_t cfg = {
    .use_mock = 0,
    .api_key = getenv("OPENAI_API_KEY"),
    .api_base = "https://api.openai.com/v1",
    .model = "gpt-4o-mini",
    .business_context = "Financial compliance audit with 3-tier scoring"
};

char* dsl = cdsl_ai_translate("check if transaction amount exceeds limit", schema, &cfg);
```

### 12.3 Streaming (SSE Callback)

```c
void on_chunk(const char* chunk, void* ud) {
    printf("%s", chunk);
    fflush(stdout);
}

// Streaming translation
char* full = cdsl_ai_translate_stream("supplier audit", schema, &cfg, on_chunk, NULL);

// Streaming review
char* review = cdsl_ai_review_stream(dsl, schema, &cfg, on_chunk, NULL);
free(full);
free(review);
```

---

## 13. Custom Actions

Register any C function as an action callback:

```c
void alert_handler(const char* name, cdsl_arg_node_t* args, void* ud) {
    const char* reason = "unknown";
    if (args && args->expr && args->expr->type == CDSL_EXPR_STRING) {
        reason = args->expr->data.string_val;
    }
    send_alert(reason);
}

cdsl_vm_register_action(vm, "send_alert", alert_handler);
```

DSL usage:
```dsl
RULE alert_rule {
    META { description = "Alert on high value transaction" }
    WHEN transaction.amount > 100000
    THEN send_alert("high_value_transaction")
}
```

---

## 14. Error Handling

### 14.1 Parse Errors

```c
cdsl_rule_t* rule = cdsl_parse_string("INVALID DSL CODE");
if (!rule) {
    // Bison prints error to stderr automatically
}
```

### 14.2 Verification Errors

```c
cdsl_error_list_t* errors = cdsl_verify_rule_detailed(rule, schema);
for (int i = 0; i < errors->count; i++) {
    cdsl_error_t* e = errors->errors[i];
    fprintf(stderr, "[%s] line %d: %s\n",
            e->kind == CDSL_ERR_TYPE ? "TYPE" : "SEMANTIC",
            e->line, e->message);
    if (e->hint) fprintf(stderr, "  hint: %s\n", e->hint);
}
cdsl_error_list_free(errors);
```

---

## 15. Thread Safety Notes

| Operation | Thread Safe |
|-----------|-------------|
| `cdsl_parse_string()` | ❌ Unsafe (Flex global state) |
| `cdsl_vm_execute()` | ✅ Safe (per-thread VM) |
| `cdsl_context_*()` | ✅ Safe (per-thread Context) |
| `cdsl_compile_cache_t` | ❌ Unsafe (protect with mutex) |
| Schema (read-only) | ✅ Safe |
| Rule (read-only) | ✅ Safe |

**Recommendation**: Create independent VM and Context per thread. Schema and Rule can be shared read-only across threads.
