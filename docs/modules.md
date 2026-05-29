# Module Design Document

## Module Overview

```
cdsl/
├── Core Modules
│   ├── ast          — Abstract syntax tree definition and construction
│   ├── abstract     — Schema verification and type checking
│   ├── execution    — VM engine, context, reports, RuleSet, codegen, visualization
│   └── ai_bridge    — AI translation and safety review (mock + LLM + streaming)
│
├── Infrastructure
│   ├── cdsl_json    — Zero-dependency JSON parser
│   ├── cdsl_error   — Structured error reporting
│   ├── cdsl_arena   — Arena memory allocator
│   └── cdsl_hashmap — Hash table implementation
│
├── Grammar
│   ├── lexer.l      — Flex lexical rules
│   └── parser.y     — Bison grammar rules
│
└── Build & Docs
    ├── CMakeLists.txt
    ├── Doxyfile
    ├── cmake/       — Package config templates
    └── docs/        — Markdown documentation
```

---

## 1. AST Module (`ast.h` / `ast.c`)

### Responsibility
Define all DSL syntax node types, provide node construction and memory free APIs.

### Core Data Structures

```
cdsl_rule_t (Rule)
├── name                   — Rule name
├── meta_list              — Metadata linked list (description, weight, ...)
├── when_expr              — WHEN expression (simple rule)
├── then_action            — THEN action (simple rule)
├── template_name          — Name of template this rule extends (or NULL)
└── metrics                — METRIC linked list (scoring rule)
    └── cdsl_metric_node_t
        ├── name           — Metric name
        ├── meta_list      — Metric metadata (weight, is_critical)
        ├── case_list      — CASE branch linked list
        │   └── cdsl_case_node_t
        │       ├── condition  — Condition expression
        │       └── action     — Hit action
        └── default_action — DEFAULT action
```

### Expression Node Types

| Type | Data | Example |
|------|------|---------|
| `CDSL_EXPR_ID` | Variable name | `user.age` |
| `CDSL_EXPR_INT` | Integer value | `18` |
| `CDSL_EXPR_FLOAT` | Float value | `3.14` |
| `CDSL_EXPR_BOOL` | Boolean value | `true` |
| `CDSL_EXPR_STRING` | String value | `"hello"` |
| `CDSL_EXPR_BINARY` | Binary operation | `a + b`, `x == y` |
| `CDSL_EXPR_UNARY` | Unary operation | `!flag` |
| `CDSL_EXPR_CALL` | Function call | `strlen(name)` |

### Key API

```c
// Parse DSL string into AST
cdsl_rule_t* cdsl_parse_string(const char* dsl_code);

// Build expression nodes
cdsl_expr_node_t* cdsl_create_expr_binary(cdsl_op_t op, cdsl_expr_node_t* left, cdsl_expr_node_t* right);
cdsl_expr_node_t* cdsl_create_expr_call(char* name, cdsl_arg_node_t* args);

// Metadata access
char* cdsl_meta_get(cdsl_meta_item_t* list, const char* key);

// Free entire rule tree
void cdsl_free_rule(cdsl_rule_t* rule);
```

---

## 2. Abstract Module (`abstract.h` / `abstract.c`)

### Responsibility
Schema registration and static rule verification. Intercepts type errors and undefined variables before execution.

### Schema Structure

```
cdsl_schema_t
├── vars (cdsl_var_schema_t linked list)
│   └── { name: "user.age", type: CDSL_TYPE_INT }
│   └── { name: "user.name", type: CDSL_TYPE_STRING }
│   └── ...
└── actions (cdsl_action_schema_t linked list)
    └── { name: "block", return: VOID, arg_count: 1, arg_types: [STRING] }
    └── { name: "score", return: VOID, arg_count: 1, arg_types: [INT] }
    └── ...
```

### Verification Flow

```
cdsl_verify_rule(rule, schema)
    │
    ├─ Walk all expression nodes
    │   └─ resolve_expr_type() → find variable type, check type compatibility
    │
    ├─ Walk all Action calls
    │   └─ verify_action() → find Action Schema, check arg count and types
    │
    └─ Return 1 (pass) or 0 (fail, err_buf contains error)
```

### Error Reporting

`cdsl_verify_rule_detailed()` returns `cdsl_error_list_t` with all found errors:

```c
cdsl_error_list_t* errors = cdsl_verify_rule_detailed(rule, schema);
for (int i = 0; i < errors->count; i++) {
    cdsl_error_print(errors->errors[i]);
}
```

### Key API

```c
cdsl_schema_t* cdsl_schema_create(void);
void cdsl_schema_free(cdsl_schema_t* schema);
void cdsl_schema_register_var(cdsl_schema_t* schema, const char* name, cdsl_type_t type);
void cdsl_schema_register_action(cdsl_schema_t* schema, const char* name,
                                  cdsl_type_t ret_type, int arg_count, ...);
int cdsl_verify_rule(const cdsl_rule_t* rule, const cdsl_schema_t* schema,
                      char* err_buf, int err_buf_sz);
cdsl_error_list_t* cdsl_verify_rule_detailed(const cdsl_rule_t* rule,
                                              const cdsl_schema_t* schema);
```

---

## 3. Execution Module (`execution.h` / `execution.c`)

### Responsibility
AST interpretation, context binding, action callback dispatch, report generation, RuleSet management, compilation cache, code generation, and visualization.

### Context

```
cdsl_context_t
├── schema   — Associated schema reference
└── entries  — Variable binding linked list
    ├── { name: "user.age", value: {INT, 25} }
    ├── { name: "user.name", value: {STRING, "Alice"} }
    └── ...
```

Two binding methods:
1. **API binding**: `cdsl_context_set_int(ctx, "user.age", 25)`
2. **JSON loading**: `cdsl_context_load_json(ctx, "{\"user\":{\"age\":25}}")`

### VM Execution Flow

```
cdsl_vm_execute(vm, rule, ctx)
    │
    ├─ Simple rule (rule->metrics == NULL)
    │   ├─ eval_expr(rule->when_expr, ctx) → bool
    │   ├─ if true: trigger_action(rule->then_action) → FAILED
    │   └─ if false: → PASSED
    │
    └─ Scoring rule (rule->metrics != NULL)
        ├─ for each metric:
        │   ├─ for each case:
        │   │   ├─ eval_expr(case.condition, ctx)
        │   │   └─ if true: trigger_action(case.action), break
        │   ├─ if no case matched: trigger_action(default_action)
        │   ├─ check is_critical → veto if failed
        │   └─ accumulate score
        ├─ aggregate total score
        ├─ check thresholds (pass_threshold, partial_threshold)
        └─ determine tri-state status
```

### Tri-state Decision Logic

```
if (any_critical_metric_failed):
    status = FAILED (veto)
else if (total_score >= pass_threshold):
    status = PASSED
else if (total_score >= partial_threshold):
    status = PARTIALLY_PASSED
else:
    status = FAILED
```

### RuleSet Batch Execution

```
cdsl_ruleset_t
├── entries (sorted by priority)
│   ├── { rule: r1, priority: 1 }  ← executes first
│   ├── { rule: r2, priority: 2 }
│   └── { rule: r3, priority: 3 }  ← executes last
└── count: 3

cdsl_vm_execute_ruleset(vm, set, ctx)
    → cdsl_ruleset_report_t (per-rule reports + aggregates)
```

### Debug Trace Mode

When enabled via `cdsl_vm_set_debug(vm, 1)`:
- Each expression evaluation prints type and value to stderr
- Each triggered action prints its name and arguments
- Each metric evaluation shows matched case and score
- Useful for debugging rule logic during development

### Hot Reload

```c
// Load from file
cdsl_ruleset_load_file(set, "path/to/rules.dsl", 1, schema, err, sizeof(err));

// Remove by name
cdsl_ruleset_remove(set, "rule_name");

// Reload file (re-parses and replaces in-place)
cdsl_ruleset_reload_file(set, "rule_name", "path/to/rules.dsl", schema, err, sizeof(err));
```

### Parallel Execution

```c
// Thread_count=0 uses hardware concurrency
cdsl_ruleset_report_t* rpt = cdsl_vm_execute_ruleset_parallel(vm, set, ctx, 4);
```

Internal implementation creates per-thread VM clones and splits rules across threads.

### Custom Function Registration

```c
// C callback
cdsl_value_t my_strlen(const char* name, cdsl_arg_node_t* args, void* ud) {
    cdsl_value_t v = { .type = CDSL_TYPE_INT, .data.int_val = 0 };
    if (args && args->expr && args->expr->type == CDSL_EXPR_STRING)
        v.data.int_val = strlen(args->expr->data.string_val);
    return v;
}

// Register
cdsl_vm_register_function(vm, "strlen", my_strlen);

// Use in DSL: WHEN strlen(user.name) > 10 THEN ...
```

### Template & Inheritance

Templates define reusable metric structures:

```
TEMPLATE base_audit {
    METRIC blacklist {
        META { weight = "30" is_critical = "true" }
        CASE supplier.is_blacklisted == false THEN score(30)
        DEFAULT fail_metric(0, "blacklisted")
    }
}

RULE supplier_audit EXTENDS base_audit {
    METRIC capital {
        META { weight = "40" }
        CASE supplier.capital >= 5000000 THEN score(40)
        DEFAULT score(0)
    }
}
```

Templates are registered globally; `EXTENDS` copies template metrics into the rule before parsing custom ones.

### Compilation Cache

```c
cdsl_compile_cache_t* cache = cdsl_compile_cache_create(64);

// Parse + verify + cache
cdsl_compiled_rule_t* cr = cdsl_compile(cache, dsl_string, schema, err, sizeof(err));

// Execute from cache (no re-parse)
cdsl_rule_report_t* rpt = cdsl_vm_execute_compiled(vm, cr, ctx);
```

Internal hash is computed from the DSL string and schema pointer for cache lookups.

### Performance Monitoring

```c
cdsl_stats_t* stats = cdsl_vm_get_stats(vm);
printf("Executions: %ld, Avg time: %.2f us\n", stats->total_executions, stats->avg_time_us);
cdsl_vm_reset_stats(vm);
```

### Code Generation (DSL → C)

```c
char* c_code = cdsl_codegen_rule_to_c(rule, schema);
// Output: C function that evaluates the rule
cdsl_codegen_to_file(rule, schema, "generated_rule.c");
```

### Visualization (Graphviz DOT)

```c
// Single rule graph
char* dot = cdsl_rule_to_dot(rule);
cdsl_rule_to_dot_file(rule, "rule.dot");

// Ruleset with dependencies
char* set_dot = cdsl_ruleset_to_dot(ruleset);
cdsl_ruleset_to_dot_file(ruleset, "ruleset.dot");
```

---

## 4. AI Bridge Module (`ai_bridge.h` / `ai_bridge.c`)

### Responsibility
Natural language to DSL translation, DSL rule safety review, with streaming support.

### Work Modes

| Mode | `use_mock` | Description |
|------|-----------|-------------|
| Mock mode | 1 | Offline keyword-based translation, structured review |
| API mode | 0 | OpenAI-compatible LLM API calls |
| Stream (API) | 0 + stream call | Callback-based SSE streaming translation/review |

### Mock Translation Logic

Keyword matching to predefined templates:

| Keywords | Generated Rule |
|----------|---------------|
| `supplier` / supplier / qualification | Supplier qualification audit rule |
| `document` / document / format | Document format audit rule |
| `content` / content / safety | Content safety audit rule |

### Safety Review Scores

Mock mode review scoring (max 70 points):

| Check | Points | Description |
|-------|--------|-------------|
| META block exists | +10 | Rule has metadata description |
| METRIC block exists | +15 | Uses multi-metric structure |
| CASE + DEFAULT complete | +15 | Each metric has full branches |
| is_critical marker | +10 | Has critical compliance item |
| weight assignment | +10 | Metrics have weight distribution |
| description field | +10 | Has functional description |

Score ≥ 50 → approved = 1

### Streaming API

```c
void my_chunk_callback(const char* chunk, void* user_data) {
    printf("%s", chunk);
    fflush(stdout);
}

char* full = cdsl_ai_translate_stream("supplier audit", schema, &cfg,
                                       my_chunk_callback, NULL);
char* review = cdsl_ai_review_stream(dsl, schema, &cfg,
                                      my_chunk_callback, NULL);
```

---

## 5. Infrastructure Modules

### 5.1 JSON Parser (`cdsl_json.h` / `cdsl_json.c`)

Zero-dependency lightweight JSON parser:
- Objects, arrays, strings, numbers, booleans, null
- Nested structures
- Used by `cdsl_context_load_json()`

### 5.2 Error Reporting (`cdsl_error.h` / `cdsl_error.c`)

Structured error types:
- `CDSL_ERR_SYNTAX` — Syntax error
- `CDSL_ERR_TYPE` — Type error
- `CDSL_ERR_SEMANTIC` — Semantic error
- `CDSL_ERR_RUNTIME` — Runtime error

### 5.3 Arena Allocator (`cdsl_arena.h` / `cdsl_arena.c`)

Batch memory allocator for same-lifetime objects (AST nodes):
- 8-byte aligned
- Default 64KB block size
- One-shot free of all memory

### 5.4 Hash Table (`cdsl_hashmap.h` / `cdsl_hashmap.c`)

O(1) average lookup key-value table:
- Separate chaining for collision resolution
- String keys, generic value pointers
- Optional destructor callback
- Used by template registry and compile cache
