# User Guide

**Revision**: 1.0 &nbsp;·&nbsp; **Audience**: Application Developers

---

## Table of Contents

- Quick Start (1)
- Core API Usage (2)
- Scoring Rules (3)
- RuleSet Batch Execution (4)
- Debug Trace Mode (5)
- Performance Monitoring (6)
- Compilation Cache (7)
- Custom Functions (8)
- Templates & Inheritance (9)
- Code Generation (10)
- Visualization (11)
- AI Integration (12)
- Custom AI Providers (13)
- Custom Cache Drivers (14)
- Custom Actions (15)
- Error Handling (16)
- Thread Safety (17)
- Bytecode VM (18)
- Sandboxing (19)
- Static Analysis (20)
- Execution Tracing (21)
- Language Server (22) (LSP)
- Document Parsing (23)

---

## 1. Quick Start

### 1.1 Requirements

| Tool     | Minimum Version |
|----------|-----------------|
| C23 compiler (GCC / Clang) | — |
| CMake    | 3.14            |
| Flex     | 2.6             |
| Bison    | 3.8             |

### 1.2 Build

```bash
git clone <repo-url>
cd cdsl
cmake -B build && cmake --build build -j$(nproc)
```

### 1.3 Run Demo

```bash
./build/cdsl_demo
```

The demo executes 6 scenarios demonstrating all major features:

| # | Scenario               | Highlights                                |
|---|------------------------|-------------------------------------------|
| 1 | Supplier Qualification | AI-generated DSL, critical blacklist, scoring |
| 2 | Document Format Audit  | AI-generated DSL, format + signature + size |
| 3 | Content Safety Audit   | AI-generated DSL, multi-metric content checks |
| 4 | JSON Context           | Variable bindings from JSON string        |
| 5 | Simple Rules           | Independent pass/fail checks              |
| 6 | RuleSet Batch          | Priority-ordered multi-rule execution     |
| 7 | Word Parser            | Word document parsing to structured JSON  |

### 1.4 Run Tests

```bash
ctest --test-dir build --output-on-failure
```

### 1.5 Generate Documentation

```bash
cmake --build build --target doc
# → build/docs/html/index.html
```

---

## 2. Core API Usage

### 2.1 Define Schema

The schema defines the contract between rules and the host program. It declares all available variables and actions.

```c
#include <cdsl/schema.h>

cdsl_schema_t* schema = cdsl_schema_create();

// Register typed variables
cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
cdsl_schema_register_var(schema, "user.name", CDSL_TYPE_STRING);
cdsl_schema_register_var(schema, "user.is_active", CDSL_TYPE_BOOL);

// Register actions with their signatures
cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
cdsl_schema_register_action(schema, "score", CDSL_TYPE_VOID, 1, CDSL_TYPE_INT);
cdsl_schema_register_action(schema, "fail_metric", CDSL_TYPE_VOID, 2,
                             CDSL_TYPE_INT, CDSL_TYPE_STRING);
```

### 2.2 Parse a Rule

```c
#include <cdsl/ast.h>

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

### 2.3 Verify the Rule

```c
// Quick verification (fail-fast)
char err[512] = {0};
if (!cdsl_verify_rule(rule, schema, err, sizeof(err))) {
    fprintf(stderr, "Verification failed: %s\n", err);
}

// Detailed verification (all errors)
cdsl_error_list_t* errors = cdsl_verify_rule_detailed(rule, schema);
if (cdsl_error_list_has_errors(errors)) {
    cdsl_error_list_print(errors);
}
cdsl_error_list_free(errors);
```

### 2.4 Create VM and Register Actions

```c
#include <cdsl/execution.h>

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
cdsl_context_load_json(ctx,
    "{\"user\":{\"age\":25,\"name\":\"Alice\",\"is_active\":true}}");

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

### 3.1 Define a Scoring Rule

```c
const char* dsl =
    "RULE scoring {"
    "  META { description = \"Scoring test\""
    "         pass_threshold = \"80\""
    "         partial_threshold = \"50\" }"
    "  METRIC m1 {"
    "    META { description = \"Metric 1\" weight = \"60\" }"
    "    CASE user.age >= 18 THEN score(60)"
    "    DEFAULT score(0)"
    "  }"
    "  METRIC m2 {"
    "    META { description = \"Metric 2\" weight = \"40\""
    "           is_critical = \"true\" }"
    "    CASE user.is_active == true THEN score(40)"
    "    DEFAULT fail_metric(0, \"inactive\")"
    "  }"
    "}";
```

### 3.2 Interpret Tri-State Results

```c
cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);

switch (rpt->status) {
    case CDSL_STATUS_PASSED:
        printf("Passed: %d/%d\n", rpt->total_obtained_score, rpt->total_max_score);
        break;

    case CDSL_STATUS_PARTIALLY_PASSED:
        printf("Partially passed: %d/%d\n",
               rpt->total_obtained_score, rpt->total_max_score);
        break;

    case CDSL_STATUS_FAILED:
        printf("Failed: %d/%d\n", rpt->total_obtained_score, rpt->total_max_score);
        for (int i = 0; i < rpt->metric_count; i++) {
            if (!rpt->metrics[i].is_passed && rpt->metrics[i].violation_reason) {
                printf("  Failed item: %s - %s\n",
                       rpt->metrics[i].metric_name,
                       rpt->metrics[i].violation_reason);
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

// Add rules with priorities (lower value = earlier execution)
cdsl_ruleset_add(set, rule_high_priority, 1);
cdsl_ruleset_add(set, rule_medium_priority, 5);
cdsl_ruleset_add(set, rule_low_priority, 10);

// Execute all
cdsl_ruleset_report_t* batch = cdsl_vm_execute_ruleset(vm, set, ctx);
cdsl_ruleset_report_print(batch);

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

// Reload (re-parses and replaces in-place)
cdsl_ruleset_reload_file(set, "check_blacklist", "rules.dsl",
                         schema, err_buf, sizeof(err_buf));
```

### 4.3 Parallel Execution

```c
// thread_count = 0 uses default (4 threads)
cdsl_ruleset_report_t* batch =
    cdsl_vm_execute_ruleset_parallel(vm, set, ctx, 0);
```

> **Note**: The context `ctx` is shared read-only across worker threads.
> Each worker gets its own short-lived VM instance. If your action
> callbacks modify context variables, create per-thread context copies
> to avoid data races.

### 4.4 Dependency Ordering

```c
// Define dependencies in META:
//   RULE r1 { META { depends_on = "r2,r3" } ... }

// Validate and topologically sort
if (cdsl_ruleset_validate_deps(set, err_buf, sizeof(err_buf))) {
    cdsl_ruleset_topo_sort(set);
}
```

---

## 5. Debug Trace Mode

Enable trace output to stderr for step-by-step expression evaluation:

```c
cdsl_vm_t* vm = cdsl_vm_create(schema);
cdsl_vm_set_debug(vm, 1);  // Enable trace

cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
```

Sample trace output:

```
[TRACE] Evaluating simple rule 'check_age'
[TRACE]   literal int: 18
[TRACE]   lookup: user.age = 25
[TRACE]   binary: 25 >= 18 = 1
[TRACE] WHEN result: true
[TRACE]   calling action: block
```

---

## 6. Performance Monitoring

```c
// After one or more executions:
cdsl_stats_t* stats = cdsl_vm_get_stats(vm);
printf("Total executions: %ld\n", stats->total_executions);
printf("Total rules:      %ld\n", stats->total_rules_executed);
printf("Total metrics:    %ld\n", stats->total_metrics_evaluated);
printf("Total time:       %.0f us\n", stats->total_time_us);
printf("Avg time/rule:    %.2f us\n", stats->avg_time_us);

// Reset counters for a new measurement period
cdsl_vm_reset_stats(vm);
```

---

## 7. Compilation Cache

Avoid re-parsing and re-verifying the same DSL string:

```c
cdsl_compile_cache_t* cache = cdsl_compile_cache_create(64);

// First call: parse + verify + cache
cdsl_compiled_rule_t* cr = cdsl_compile(cache, dsl_string, schema,
                                         err_buf, sizeof(err_buf));
if (!cr) {
    fprintf(stderr, "Compile failed: %s\n", err_buf);
    return;
}

// Execute from cache (no re-parse)
cdsl_rule_report_t* rpt = cdsl_vm_execute_compiled(vm, cr, ctx);

// Subsequent calls with the same DSL string hit the cache

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

### 8.1 Built-in Functions

CDSL provides these functions out of the box (auto-registered by `cdsl_vm_create()`):

**String functions:**

| Function                | Args      | Return | Description |
|-------------------------|-----------|--------|-------------|
| `strlen(s)`             | STRING    | INT    | Length of string `s` |
| `contains(haystack, needle)` | STRING, STRING | INT | 1 if `haystack` contains `needle`, else 0 |
| `uppercase(s)`          | STRING    | STRING | Convert string to uppercase |
| `lowercase(s)`          | STRING    | STRING | Convert string to lowercase |
| `trim(s)`               | STRING    | STRING | Remove leading and trailing whitespace |
| `startswith(s, prefix)` | STRING, STRING | INT | 1 if `s` starts with `prefix`, else 0 |
| `endswith(s, suffix)`   | STRING, STRING | INT | 1 if `s` ends with `suffix`, else 0 |

**Date functions:**

| Function                | Args      | Return | Description |
|-------------------------|-----------|--------|-------------|
| `now()`                 | —         | DATE   | Current time as `time_t` |
| `is_before(a, b)`       | DATE/STRING, DATE/STRING | INT | 1 if `a < b`, else 0 |
| `is_after(a, b)`        | DATE/STRING, DATE/STRING | INT | 1 if `a > b`, else 0 |
| `days_between(a, b)`    | DATE/STRING, DATE/STRING | INT | Absolute days between `a` and `b` |
| `date_add(d, days)`     | DATE/STRING, INT | DATE | Add `days` to date `d` |

**Math functions:**

| Function                | Args      | Return | Description |
|-------------------------|-----------|--------|-------------|
| `abs(n)`                | INT/FLOAT | INT/FLOAT | Absolute value |
| `min(a, b)`             | INT/FLOAT, INT/FLOAT | INT/FLOAT | Smaller of two values |
| `max(a, b)`             | INT/FLOAT, INT/FLOAT | INT/FLOAT | Larger of two values |
| `round(n)`              | FLOAT     | INT    | Round to nearest integer |

**Type introspection:**

| Function                | Args      | Return | Description |
|-------------------------|-----------|--------|-------------|
| `typeof(expr)`          | ANY       | STRING | Type as string ("INT","FLOAT","BOOL","STRING","DATE","VOID") |

DSL usage:

```dsl
RULE check_name {
    META { description = "Name validation" }
    WHEN strlen(user.name) > 0
    THEN record_warning("name_exists")
}
```

---

## 9. Templates & Inheritance

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

Template metrics are deep-copied into the extending rule during parsing. Custom metrics defined in the rule body are appended after inherited ones.

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

### 12.1 Offline Mode (Mock)

```c
#include <cdsl/ai.h>

// Default config uses mock mode (use_mock = 1)
cdsl_ai_config_t cfg = cdsl_ai_config_default();

// Translate natural language to DSL
char* dsl = cdsl_ai_translate("supplier qualification audit", schema, &cfg);
printf("Generated DSL:\n%s\n", dsl);

// Optional business context guides generation
cfg.business_context =
    "Evaluate supplier capital, blacklist status, and experience";
char* dsl2 = cdsl_ai_translate("supplier audit", schema, &cfg);

// Rule safety review
cdsl_ai_review_t* review = cdsl_ai_review(dsl, schema, &cfg);
printf("Approved: %s, Risk: %d/100\n",
       review->approved ? "YES" : "NO", review->risk_score);

cdsl_ai_review_free(review);
free(dsl);
```

The offline mode generates one metric per registered schema variable with type-appropriate CASE conditions.

### 12.2 API Mode (Real LLM)

```c
cdsl_ai_config_t cfg = {
    .use_mock = 0,
    .api_key = getenv("OPENAI_API_KEY"),
    .api_base = "https://api.openai.com/v1",
    .model = "gpt-4o-mini",
    .business_context = "Financial compliance audit with 3-tier scoring"
};

char* dsl = cdsl_ai_translate(
    "check if transaction amount exceeds limit", schema, &cfg);
```

### 12.3 Custom AI Providers

Register a custom AI provider to replace the built-in LLM or mock backends:

```c
#include <cdsl/ai.h>

cdsl_ai_provider_t my_prov = {
    .ctx = my_state,
    .translate = my_translate_fn,
    .review = my_review_fn,
    .translate_stream = NULL,
    .review_stream = NULL,
};
cdsl_ai_register_provider("my_provider", &my_prov);

cdsl_ai_config_t cfg = cdsl_ai_config_default();
cfg.provider_name = "my_provider";
cfg.use_mock = 0;

char* dsl = cdsl_ai_translate("check transaction limit", schema, &cfg);
// → calls my_translate_fn(...)
```

### 12.4 Custom Cache Drivers

Avoid redundant LLM calls by registering or injecting an external cache:

```c
// Option 1: Register a named cache driver
cdsl_ai_cache_t redis_cache = { .ctx = redis_conn, .get = redis_get, .put = redis_set };
cdsl_ai_register_cache_driver("redis", &redis_cache);

cfg.cache_driver_name = "redis";  // Dispatch via registry

// Option 2: Inject a cache instance directly
cdsl_ai_cache_t file_cache = { .ctx = NULL, .get = file_get, .put = file_set };
cfg.cache = &file_cache;  // Bypasses the registry
```

### 12.5 Streaming (SSE Callback)

```c
void on_chunk(const char* chunk, void* ud) {
    printf("%s", chunk);
    fflush(stdout);
}

// Streaming translation
char* full = cdsl_ai_translate_stream(
    "supplier audit", schema, &cfg, on_chunk, NULL);

// Streaming review
char* review = cdsl_ai_review_stream(
    dsl, schema, &cfg, on_chunk, NULL);

free(full);
free(review);
```

---

## 15. Custom Actions

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

## 16. Error Handling

### 16.1 Parse Errors

```c
cdsl_rule_t* rule = cdsl_parse_string("INVALID DSL CODE");
if (!rule) {
    // Bison prints error information to stderr automatically
}
```

### 16.2 Verification Errors

```c
cdsl_error_list_t* errors = cdsl_verify_rule_detailed(rule, schema);
for (int i = 0; i < errors->count; i++) {
    cdsl_error_t* e = errors->errors[i];
    fprintf(stderr, "[%s] line %d: %s\n",
            e->kind == CDSL_ERR_TYPE ? "TYPE" : "SEMANTIC",
            e->line, e->message);
    if (e->hint) {
        fprintf(stderr, "  hint: %s\n", e->hint);
    }
}
cdsl_error_list_free(errors);
```

---

## 17. Thread Safety Notes

> **Key principle**: One VM + one Context per thread. Schema and parsed
> Rule objects are read-only and can be shared across threads after parsing.

| Operation                     | Thread Safe | Guidance                          |
|-------------------------------|-------------|-----------------------------------|
| `cdsl_parse_string()`         | ✅          | Reentrant scanner; thread-local arena |
| `cdsl_vm_execute()`           | ✅          | One VM per thread; VM stats use `_Atomic` counters |
| `cdsl_vm_execute_compiled()`  | ✅          | Same guarantees as `cdsl_vm_execute()` |
| `cdsl_context_set_*()`        | ✅          | One Context per thread (no internal locking) |
| `cdsl_context_get_*()`        | ✅          | Read-only; safe with concurrent readers |
| `cdsl_context_load_json()`    | ✅          | One Context per thread |
| `cdsl_schema_*()`             | ⚠️ Read-only | Schema should not be modified after being shared |
| `cdsl_rule_t` / AST           | ✅          | Immutable after parsing; read-only sharing |
| `cdsl_compile_cache_t`        | ✅          | Internal RWLock protection |
| `cdsl_hashmap_*()`            | ❌           | Not thread-safe; use `cdsl_compile_cache_t` for concurrent access |
| AI provider/cache registry    | ✅          | pthread_once init; RWLock-guarded |
| `cdsl_template_*()`           | ✅          | Internal RWLock protection |

> **Parallel execution**: `cdsl_vm_execute_ruleset_parallel()` creates
> per-worker VM instances. The context is shared read-only across workers.
> Stats are aggregated from worker VMs back to the parent VM via `_Atomic`
> operations — do not call this function concurrently on the same VM instance.
>
> **When libcurl is unavailable**, the AI bridge falls back to `popen("curl ...")`.
> This path is inherently less secure than libcurl. Prefer building with
> libcurl for production deployments.

**Recommendation**: Create independent VM and Context instances per thread.
Schema and Rule objects can be shared read-only across threads.
For thread-safe hash map operations, use `cdsl_compile_cache_t`.

---

## 18. Bytecode VM

The bytecode VM compiles DSL rule expressions into a flat instruction
stream for faster execution via a stack-based virtual machine.

### 18.1 Usage

Bytecode is generated automatically by `cdsl_compile()` (the compilation
cache).  When you call `cdsl_vm_execute_compiled()`, the cached bytecode
is used instead of the recursive tree-walk evaluator.

```c
cdsl_compile_cache_t* cache = cdsl_compile_cache_create(128);
cdsl_compiled_rule_t* compiled = cdsl_compile(cache, dsl_string, schema, NULL, 0);
cdsl_rule_report_t* report = cdsl_vm_execute_compiled(vm, compiled, ctx);
//  ↑ executes via bytecode VM (fast path)
```

For `cdsl_vm_execute()` (no caching), the recursive tree-walk evaluator
is always used.

### 18.2 Managing cache entries

```c
cdsl_compile_cache_remove(cache, dsl_string); // evict one entry
cdsl_compile_cache_free(cache);               // evict all
```

### 18.3 Bytecode features

- **Constant folding**: `2+3`, `@2024-01-10 - @2024-01-01` → evaluated at compile time
- **Short-circuit AND/OR**: compiled to conditional jumps (no recursion)
- **24-instruction stack ISA** including arithmetic, comparison, control flow, and function calls
- **Low-level API**: `cdsl_bytecode_compile()`, `cdsl_bytecode_execute()`, `cdsl_bytecode_free()`

---

## 19. Sandboxing

The VM supports per-execution resource quotas to prevent malicious or
misconfigured DSL rules from exhausting system resources.

### 19.1 Timeout

```c
cdsl_vm_set_timeout(vm, 5000000);  // 5-second timeout (in microseconds)
```

When the timeout is exceeded, execution aborts and returns
`CDSL_STATUS_ERROR`.  Timeout is checked every 1024 bytecode instructions
and at rule entry for tree-walk evaluation.

### 19.2 Memory limit

```c
cdsl_vm_set_memory_limit(vm, 1048576);  // 1 MB per execution
```

Allocations exceeding this cap cause abort with `CDSL_STATUS_ERROR`.
The counters are `_Atomic` for correct concurrent access.

### 19.3 Read-only variables

```c
cdsl_schema_register_var_rw(schema, "config.api_key", CDSL_TYPE_STRING, 1 /* readonly */);
cdsl_context_set_string(ctx, "config.api_key", "abc");  // ok (initial bind)
cdsl_context_set_string(ctx, "config.api_key", "xyz");  // silently ignored (readonly)
```

---

## 20. Static Analysis

Beyond `cdsl_verify_rule()`, use `cdsl_analyze_rule()` to detect warnings:

```c
cdsl_error_list_t* warnings = cdsl_analyze_rule(rule, schema);
if (warnings) {
    for (int i = 0; i < warnings->count; i++)
        printf("WARN: %s\n", warnings->errors[i]->message);
    cdsl_error_list_free(warnings);
}
```

**Detected issues:**
- Dead CASE branches (always-true / always-false conditions)
- Shadowed CASE conditions (identical comparisons)
- Tautologies (WHEN that always triggers)
- Contradictions (WHEN that never triggers)

---

## 21. Execution Tracing

Register a callback to receive step-by-step execution events:

```c
void my_trace(const cdsl_trace_event_t* ev, void* ud) {
    printf("[%s] %s → %d (%.0fus)\n",
           ev->rule_name, ev->detail, ev->value.data.int_val, ev->timestamp_us);
}

cdsl_vm_set_trace_callback(vm, my_trace, NULL);
cdsl_vm_execute(vm, rule, ctx);
// Output: [check_age] WHEN → 1 (124.5us)
// Output: [check_age] block → 0 (126.1us)
// Output: [check_age] PASSED → 0 (127.3us)
```

---

## 22. Language Server (LSP)

The C-DSL Language Server provides real-time diagnostics and completions
for editors supporting the Language Server Protocol (Neovim, VSCode, etc.).

```bash
# Build with LSP support
cmake -DCDSL_BUILD_LSP=ON .. && make cdsl-lsp

# Configure your editor (example: Neovim with lspconfig)
# vim.api.nvim_create_autocmd('FileType', {
#   pattern = 'dsl',
#   callback = function()
#     vim.lsp.start({ name = 'cdsl', cmd = { 'cdsl-lsp' } })
#   end,
# })
```

**Features:**
- Parse errors and schema violations as in-editor diagnostics
- Keyword, built-in function, schema variable, and action completions
- Hover info showing variable types and read-only status
- Full LSP lifecycle (initialize, didOpen/Change/Close, shutdown/exit)

---

## 23. Document Parsing (Word .docx)

Extract structured content from Word documents for rule-based evaluation.

### 23.1 Initialization

```c
#include <cdsl/doc.h>

// Initialize LibreOffice runtime (idempotent, thread-safe)
if (!cdsl_doc_init()) {
    fprintf(stderr, "Failed to initialize LibreOfficeKit\n");
    return;
}

// ... use parser ...

// Shut down when done
cdsl_doc_shutdown();
```

### 23.2 Extract Plain Text

```c
char* text = cdsl_doc_extract_text("/path/to/document.docx");
if (text) {
    printf("Document text:\n%s\n", text);
    cdsl_doc_free_string(text);
}
```

### 23.3 Extract Structured JSON

```c
char* json = cdsl_doc_extract_to_json("/path/to/document.docx");
if (json) {
    printf("Structured JSON:\n%s\n", json);

    // The JSON is compatible with cdsl_context_load_json()
    cdsl_context_t* ctx = cdsl_context_create(schema);
    cdsl_context_load_json(ctx, json);

    // Now use ctx for rule evaluation
    // e.g., cdsl_context_get_int(ctx, "document.metadata.page_count", 0);

    cdsl_context_free(ctx);
    cdsl_doc_free_string(json);
}
```

### 23.4 JSON Output Structure

```json
{
  "document": {
    "pages": [
      {
        "page_number": 1,
        "width_mm": 210,
        "height_mm": 297,
        "margin_top_mm": 20,
        "margin_bottom_mm": 20,
        "margin_left_mm": 30,
        "margin_right_mm": 30,
        "paragraphs": [
          {
            "style": "Standard",
            "alignment": "left",
            "spacing_before_mm": 0,
            "spacing_after_mm": 0,
            "line_spacing": 1.15,
            "indent_first_line_mm": 0,
            "bbox_mm": [35.0, 25.0, 145.0, 4.7],
            "text_blocks": [
              {
                "text": "Hello, C-DSL!",
                "font_name": "Liberation Serif",
                "font_size_pt": 12.0,
                "bold": false,
                "italic": false,
                "underline": false,
                "strikethrough": false,
                "color": "#000000",
                "bbox_mm": [35.0, 25.0, 145.0, 4.7]
              }
            ]
          }
        ]
      }
    ],
    "metadata": {
      "page_count": 1,
      "paragraph_count": 2,
      "word_count": 13,
      "character_count": 79
    },
    "full_text": "Hello, C-DSL!\nThis is a test document."
  }
}
```

### 23.5 Evaluate Document Content with Rules

```c
// Define a schema for document properties
cdsl_schema_t* doc_schema = cdsl_schema_create();
cdsl_schema_register_var(doc_schema, "document.metadata.page_count", CDSL_TYPE_INT);
cdsl_schema_register_var(doc_schema, "document.metadata.paragraph_count", CDSL_TYPE_INT);
cdsl_schema_register_var(doc_schema, "document.metadata.word_count", CDSL_TYPE_INT);
cdsl_schema_register_var(doc_schema, "document.full_text", CDSL_TYPE_STRING);

// Parse document structure into context
char* json = cdsl_doc_extract_to_json("report.docx");
cdsl_context_t* ctx = cdsl_context_create(doc_schema);
cdsl_context_load_json(ctx, json);

// Evaluate against rules
cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, doc_rule, ctx);

cdsl_report_print(rpt);
cdsl_report_free(rpt);
cdsl_context_free(ctx);
cdsl_doc_free_string(json);
```

