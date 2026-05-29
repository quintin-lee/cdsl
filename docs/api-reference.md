# API Reference Manual

## Module Index

| Module | Header | Description |
|--------|--------|-------------|
| [AST](#ast-module) | `ast.h` | Abstract syntax tree definition |
| [Abstract](#abstract-module) | `abstract.h` | Schema verification |
| [Execution](#execution-module) | `execution.h` | VM, context, reports, RuleSet, codegen, visualization |
| [AI Bridge](#ai-bridge-module) | `ai_bridge.h` | AI integration (mock + LLM + streaming) |
| [Error](#error-module) | `cdsl_error.h` | Error reporting |
| [Arena](#arena-module) | `cdsl_arena.h` | Memory allocator |
| [Hashmap](#hashmap-module) | `cdsl_hashmap.h` | Hash table |
| [JSON](#json-module) | `cdsl_json.h` | JSON parser |

---

## AST Module

### Type Definitions

#### `cdsl_type_t`
```c
typedef enum {
    CDSL_TYPE_INT,    // 32-bit integer
    CDSL_TYPE_FLOAT,  // 64-bit float
    CDSL_TYPE_BOOL,   // boolean
    CDSL_TYPE_STRING, // string
    CDSL_TYPE_VOID    // void
} cdsl_type_t;
```

#### `cdsl_op_t`
```c
typedef enum {
    CDSL_OP_EQ, CDSL_OP_NE, CDSL_OP_LT, CDSL_OP_GT,
    CDSL_OP_LE, CDSL_OP_GE,
    CDSL_OP_AND, CDSL_OP_OR, CDSL_OP_NOT
} cdsl_op_t;
```

#### `cdsl_rule_t`
```c
typedef struct cdsl_rule {
    char* name;
    cdsl_meta_item_t* meta_list;
    cdsl_expr_node_t* when_expr;
    cdsl_action_node_t* then_action;
    cdsl_metric_node_t* metrics;
    char* template_name;
} cdsl_rule_t;
```

### Functions

#### `cdsl_parse_string`
```c
cdsl_rule_t* cdsl_parse_string(const char* dsl_code);
```
Parse a DSL string into an AST rule. Returns NULL on error.

#### `cdsl_register_template`
```c
void cdsl_register_template(const char* name, cdsl_rule_t* rule);
```
Register a TEMPLATE rule for EXTENDS lookup during parsing.

#### `cdsl_create_simple_rule`
```c
cdsl_rule_t* cdsl_create_simple_rule(char* name, cdsl_meta_item_t* meta,
                                      cdsl_expr_node_t* when, cdsl_action_node_t* then);
```
Create a simple WHEN/THEN rule.

#### `cdsl_create_metric_rule`
```c
cdsl_rule_t* cdsl_create_metric_rule(char* name, cdsl_meta_item_t* meta,
                                      cdsl_metric_node_t* metrics);
```
Create a multi-metric scoring rule.

#### `cdsl_meta_get`
```c
char* cdsl_meta_get(cdsl_meta_item_t* list, const char* key);
```
Find a metadata value by key. Returns NULL if not found.

#### `cdsl_free_rule`
```c
void cdsl_free_rule(cdsl_rule_t* rule);
```
Free a rule and all of its child nodes.

---

## Abstract Module

### Type Definitions

#### `cdsl_schema_t`
```c
typedef struct cdsl_schema {
    cdsl_var_schema_t* vars;
    cdsl_action_schema_t* actions;
} cdsl_schema_t;
```

#### `cdsl_var_schema_t`
```c
typedef struct cdsl_var_schema {
    char* name;
    cdsl_type_t type;
    struct cdsl_var_schema* next;
} cdsl_var_schema_t;
```

#### `cdsl_action_schema_t`
```c
typedef struct cdsl_action_schema {
    char* name;
    cdsl_type_t return_type;
    int arg_count;
    cdsl_type_t* arg_types;
    struct cdsl_action_schema* next;
} cdsl_action_schema_t;
```

### Functions

#### `cdsl_schema_create`
```c
cdsl_schema_t* cdsl_schema_create(void);
```
Create an empty schema.

#### `cdsl_schema_free`
```c
void cdsl_schema_free(cdsl_schema_t* schema);
```
Free a schema and all registered variables/actions.

#### `cdsl_schema_register_var`
```c
void cdsl_schema_register_var(cdsl_schema_t* schema, const char* name, cdsl_type_t type);
```
Register a variable with its type.

#### `cdsl_schema_register_action`
```c
void cdsl_schema_register_action(cdsl_schema_t* schema, const char* name,
                                  cdsl_type_t ret_type, int arg_count, ...);
```
Register an action with its signature. Variadic args are `cdsl_type_t` values for argument types.

#### `cdsl_verify_rule`
```c
int cdsl_verify_rule(const cdsl_rule_t* rule, const cdsl_schema_t* schema,
                      char* err_buf, int err_buf_sz);
```
Verify a rule against the schema. Returns 1 if valid, 0 on error with message in err_buf.

#### `cdsl_verify_rule_detailed`
```c
cdsl_error_list_t* cdsl_verify_rule_detailed(const cdsl_rule_t* rule,
                                               const cdsl_schema_t* schema);
```
Verify with detailed error collection (all errors, not just first). Returns error list to inspect or print.

---

## Execution Module

### Type Definitions

#### `cdsl_value_t`
```c
typedef struct cdsl_value {
    cdsl_type_t type;
    union {
        int int_val;
        double float_val;
        int bool_val;
        char* string_val;
    } data;
} cdsl_value_t;
```

#### `cdsl_rule_status_t`
```c
typedef enum {
    CDSL_STATUS_PASSED,
    CDSL_STATUS_PARTIALLY_PASSED,
    CDSL_STATUS_FAILED,
    CDSL_STATUS_ERROR
} cdsl_rule_status_t;
```

#### `cdsl_rule_report_t`
```c
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
```

#### `cdsl_metric_result_t`
```c
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
```

#### `cdsl_stats_t`
```c
typedef struct {
    long total_executions;
    long total_rules_executed;
    long total_metrics_evaluated;
    long total_actions_triggered;
    double total_time_us;
    double avg_time_us;
} cdsl_stats_t;
```

#### `cdsl_compiled_rule_t`
```c
typedef struct cdsl_compiled_rule {
    cdsl_rule_t* rule;
    char* dsl_hash;
    int verified;
} cdsl_compiled_rule_t;
```

#### `cdsl_compile_cache_t`
```c
typedef struct cdsl_compile_cache {
    cdsl_compiled_rule_t** entries;
    int count;
    int capacity;
} cdsl_compile_cache_t;
```

#### `cdsl_ruleset_report_t`
```c
typedef struct {
    cdsl_rule_report_t** rule_reports;
    int rule_count;
    int total_passed;
    int total_partially;
    int total_failed;
    int aggregate_score;
    int aggregate_max;
    char* summary;
} cdsl_ruleset_report_t;
```

### Context Management

#### `cdsl_context_create`
```c
cdsl_context_t* cdsl_context_create(const cdsl_schema_t* schema);
```
Create an empty context bound to a schema.

#### `cdsl_context_free`
```c
void cdsl_context_free(cdsl_context_t* ctx);
```
Free a context and all its variable bindings.

#### `cdsl_context_set_int` / `cdsl_context_set_float` / `cdsl_context_set_bool` / `cdsl_context_set_string`
```c
void cdsl_context_set_int(cdsl_context_t* ctx, const char* name, int val);
void cdsl_context_set_float(cdsl_context_t* ctx, const char* name, double val);
void cdsl_context_set_bool(cdsl_context_t* ctx, const char* name, int val);
void cdsl_context_set_string(cdsl_context_t* ctx, const char* name, const char* val);
```
Set a context variable value by name. Replaces existing binding if present.

#### `cdsl_context_load_json`
```c
int cdsl_context_load_json(cdsl_context_t* ctx, const char* json_str);
```
Load variables from a JSON string. Supports nested objects (flattened with dot notation). Returns 1 on success.

### Virtual Machine

#### `cdsl_vm_create`
```c
cdsl_vm_t* cdsl_vm_create(const cdsl_schema_t* schema);
```
Create a virtual machine instance. One VM per thread recommended.

#### `cdsl_vm_free`
```c
void cdsl_vm_free(cdsl_vm_t* vm);
```
Free a VM and all registered callbacks.

#### `cdsl_vm_register_action`
```c
void cdsl_vm_register_action(cdsl_vm_t* vm, const char* action_name, cdsl_action_cb_t cb);
```
Register an action callback triggered by THEN/DEFAULT/CASE action execution.

#### `cdsl_vm_register_function`
```c
void cdsl_vm_register_function(cdsl_vm_t* vm, const char* func_name, cdsl_func_cb_t cb);
```
Register a custom function callback usable in expressions (e.g., `strlen(x)`).

#### `cdsl_vm_set_debug`
```c
void cdsl_vm_set_debug(cdsl_vm_t* vm, int enabled);
```
Enable or disable debug trace output to stderr. When enabled, each expression evaluation prints type and value.

#### `cdsl_vm_get_stats`
```c
cdsl_stats_t* cdsl_vm_get_stats(const cdsl_vm_t* vm);
```
Get execution statistics for this VM. Returns pointer to internal stats struct.

#### `cdsl_vm_reset_stats`
```c
void cdsl_vm_reset_stats(cdsl_vm_t* vm);
```
Reset all execution statistics counters to zero.

### Rule Execution

#### `cdsl_vm_execute`
```c
cdsl_rule_report_t* cdsl_vm_execute(cdsl_vm_t* vm, const cdsl_rule_t* rule, cdsl_context_t* ctx);
```
Execute a single rule. Returns report with per-metric results and tri-state decision.

#### `cdsl_report_free`
```c
void cdsl_report_free(cdsl_rule_report_t* report);
```
Free a rule execution report.

#### `cdsl_report_print`
```c
void cdsl_report_print(const cdsl_rule_report_t* report);
```
Print a human-readable report to stdout.

#### `cdsl_report_to_json`
```c
char* cdsl_report_to_json(const cdsl_rule_report_t* report);
```
Serialize report to JSON string. Must be freed with `free()`.

### Compilation Cache

#### `cdsl_compile_cache_create`
```c
cdsl_compile_cache_t* cdsl_compile_cache_create(int capacity);
```
Create a compilation cache. Passing 0 uses a default capacity of 64.

#### `cdsl_compile_cache_free`
```c
void cdsl_compile_cache_free(cdsl_compile_cache_t* cache);
```
Free the cache and all stored compiled rules.

#### `cdsl_compile`
```c
cdsl_compiled_rule_t* cdsl_compile(cdsl_compile_cache_t* cache, const char* dsl_code,
                                    const cdsl_schema_t* schema, char* err_buf, int err_buf_sz);
```
Parse, verify, and cache a DSL string. Returns compiled rule on success, NULL on failure. Subsequent calls with the same DSL string return the cached result.

#### `cdsl_vm_execute_compiled`
```c
cdsl_rule_report_t* cdsl_vm_execute_compiled(cdsl_vm_t* vm, cdsl_compiled_rule_t* compiled,
                                              cdsl_context_t* ctx);
```
Execute a compiled rule from the cache. Avoids re-parsing and re-verification.

### Code Generation

#### `cdsl_codegen_rule_to_c`
```c
char* cdsl_codegen_rule_to_c(const cdsl_rule_t* rule, const cdsl_schema_t* schema);
```
Generate equivalent C source code from a DSL rule. Returns malloc'd string.

#### `cdsl_codegen_to_file`
```c
int cdsl_codegen_to_file(const cdsl_rule_t* rule, const cdsl_schema_t* schema, const char* filepath);
```
Generate C code and write it to a file. Returns 1 on success.

### Visualization

#### `cdsl_rule_to_dot`
```c
char* cdsl_rule_to_dot(const cdsl_rule_t* rule);
```
Generate Graphviz DOT graph for a single rule. Node colors: blue=variables, green=conditions, pink=functions, yellow=literals.

#### `cdsl_rule_to_dot_file`
```c
int cdsl_rule_to_dot_file(const cdsl_rule_t* rule, const char* filepath);
```
Write DOT graph to file. Returns 1 on success.

#### `cdsl_ruleset_to_dot`
```c
char* cdsl_ruleset_to_dot(const cdsl_ruleset_t* set);
```
Generate DOT graph for a ruleset with dependency arrows.

#### `cdsl_ruleset_to_dot_file`
```c
int cdsl_ruleset_to_dot_file(const cdsl_ruleset_t* set, const char* filepath);
```
Write ruleset DOT graph to file. Returns 1 on success.

### RuleSet Management

#### `cdsl_ruleset_create`
```c
cdsl_ruleset_t* cdsl_ruleset_create(void);
```
Create an empty ruleset.

#### `cdsl_ruleset_free`
```c
void cdsl_ruleset_free(cdsl_ruleset_t* set);
```
Free a ruleset. Rules within the set are NOT freed (caller owns them).

#### `cdsl_ruleset_add`
```c
void cdsl_ruleset_add(cdsl_ruleset_t* set, cdsl_rule_t* rule, int priority);
```
Add a rule to a ruleset. Lower priority = executes first. Rules are inserted in sorted order.

#### `cdsl_ruleset_remove`
```c
int cdsl_ruleset_remove(cdsl_ruleset_t* set, const char* rule_name);
```
Remove a rule from the set by name. Returns 1 if found and removed, 0 otherwise. Does NOT free the rule.

#### `cdsl_vm_execute_ruleset`
```c
cdsl_ruleset_report_t* cdsl_vm_execute_ruleset(cdsl_vm_t* vm, cdsl_ruleset_t* set, cdsl_context_t* ctx);
```
Execute all rules in a ruleset in priority order. Returns a batch report with aggregate scores.

#### `cdsl_vm_execute_ruleset_parallel`
```c
cdsl_ruleset_report_t* cdsl_vm_execute_ruleset_parallel(cdsl_vm_t* vm, cdsl_ruleset_t* set,
                                                          cdsl_context_t* ctx, int thread_count);
```
Execute rules in parallel using the specified number of threads. Pass 0 to use hardware concurrency. Creates internal per-thread VM clones.

#### `cdsl_ruleset_report_free`
```c
void cdsl_ruleset_report_free(cdsl_ruleset_report_t* report);
```
Free a batch execution report.

#### `cdsl_ruleset_report_print`
```c
void cdsl_ruleset_report_print(const cdsl_ruleset_report_t* report);
```
Print a human-readable batch report to stdout.

#### `cdsl_ruleset_load_file`
```c
int cdsl_ruleset_load_file(cdsl_ruleset_t* set, const char* filepath, int priority,
                            const cdsl_schema_t* schema, char* err_buf, int err_buf_sz);
```
Parse a DSL file and add the resulting rule to a ruleset. Returns 1 on success.

#### `cdsl_ruleset_load_string`
```c
int cdsl_ruleset_load_string(cdsl_ruleset_t* set, const char* dsl_code, int priority,
                              const cdsl_schema_t* schema, char* err_buf, int err_buf_sz);
```
Parse a DSL string and add the resulting rule to a ruleset. Returns 1 on success.

#### `cdsl_ruleset_reload_file`
```c
int cdsl_ruleset_reload_file(cdsl_ruleset_t* set, const char* rule_name,
                              const char* filepath, const cdsl_schema_t* schema,
                              char* err_buf, int err_buf_sz);
```
Re-parse a DSL file and replace an existing rule in the set. Old rule is freed. Returns 1 on success.

#### `cdsl_ruleset_validate_deps`
```c
int cdsl_ruleset_validate_deps(const cdsl_ruleset_t* set, char* err_buf, int err_buf_sz);
```
Validate that all dependency references in `depends_on` meta resolve to existing rules in the set.

#### `cdsl_ruleset_topo_sort`
```c
int cdsl_ruleset_topo_sort(cdsl_ruleset_t* set);
```
Topologically sort rules based on `depends_on` meta. Returns 1 on success, 0 if cycle detected.

---

## AI Bridge Module

### Type Definitions

#### `cdsl_ai_config_t`
```c
typedef struct {
    int use_mock;
    char* api_key;
    char* api_base;
    char* model;
} cdsl_ai_config_t;
```

#### `cdsl_ai_review_t`
```c
typedef struct {
    int approved;
    int risk_score;
    char* reason;
    char* suggestions;
} cdsl_ai_review_t;
```

#### `cdsl_ai_stream_cb_t`
```c
typedef void (*cdsl_ai_stream_cb_t)(const char* chunk, void* user_data);
```
Callback for streaming AI responses. Called for each text chunk received.

### Functions

#### `cdsl_ai_config_default`
```c
cdsl_ai_config_t cdsl_ai_config_default(void);
```
Return default config with mock mode enabled (`use_mock = 1`).

#### `cdsl_ai_translate`
```c
char* cdsl_ai_translate(const char* natural_language,
                         const cdsl_schema_t* schema,
                         const cdsl_ai_config_t* config);
```
Translate natural language to DSL code. In mock mode uses keyword matching; in API mode calls LLM. Must be freed with `free()`.

#### `cdsl_ai_review`
```c
cdsl_ai_review_t* cdsl_ai_review(const char* dsl_code,
                                   const cdsl_schema_t* schema,
                                   const cdsl_ai_config_t* config);
```
Review DSL code for safety and correctness. Analyzes structure, missing elements, and risk factors. Must be freed with `cdsl_ai_review_free()`.

#### `cdsl_ai_review_free`
```c
void cdsl_ai_review_free(cdsl_ai_review_t* review);
```
Free an AI review result.

#### `cdsl_ai_translate_stream`
```c
char* cdsl_ai_translate_stream(const char* natural_language,
                                const cdsl_schema_t* schema,
                                const cdsl_ai_config_t* config,
                                cdsl_ai_stream_cb_t callback, void* user_data);
```
Streaming NL-to-DSL translation. Chunks delivered via callback. Returns complete response string or NULL on error. In mock mode, falls back to non-streaming implementation.

#### `cdsl_ai_review_stream`
```c
char* cdsl_ai_review_stream(const char* dsl_code,
                             const cdsl_schema_t* schema,
                             const cdsl_ai_config_t* config,
                             cdsl_ai_stream_cb_t callback, void* user_data);
```
Streaming DSL safety review. Chunks delivered via callback. Returns complete response string or NULL on error. In mock mode, falls back to non-streaming implementation.

---

## Error Module

### Type Definitions

#### `cdsl_error_kind_t`
```c
typedef enum {
    CDSL_ERR_SYNTAX,   // Parse error
    CDSL_ERR_TYPE,     // Type mismatch
    CDSL_ERR_SEMANTIC, // Unknown variable/action
    CDSL_ERR_RUNTIME   // Runtime error
} cdsl_error_kind_t;
```

#### `cdsl_error_t`
```c
typedef struct {
    cdsl_error_kind_t kind;
    int line;
    int column;
    char* message;
    char* hint;
} cdsl_error_t;
```

#### `cdsl_error_list_t`
```c
typedef struct {
    cdsl_error_t** errors;
    int count;
    int capacity;
} cdsl_error_list_t;
```

### Functions

#### `cdsl_error_create`
```c
cdsl_error_t* cdsl_error_create(cdsl_error_kind_t kind, int line, int column,
                                  const char* message, const char* hint);
```
Create an error instance. Copies message and hint strings.

#### `cdsl_error_free`
```c
void cdsl_error_free(cdsl_error_t* err);
```
Free a single error.

#### `cdsl_error_print`
```c
void cdsl_error_print(const cdsl_error_t* err);
```
Print an error to stdout in a formatted way.

#### `cdsl_error_list_create`
```c
cdsl_error_list_t* cdsl_error_list_create(void);
```
Create an empty error list.

#### `cdsl_error_list_add`
```c
void cdsl_error_list_add(cdsl_error_list_t* list, cdsl_error_t* err);
```
Add an error to the list.

#### `cdsl_error_list_free`
```c
void cdsl_error_list_free(cdsl_error_list_t* list);
```
Free an error list and all contained errors.

#### `cdsl_error_list_print`
```c
void cdsl_error_list_print(const cdsl_error_list_t* list);
```
Print all errors in a list.

---

## Arena Module

### Functions

#### `cdsl_arena_create`
```c
cdsl_arena_t* cdsl_arena_create(size_t block_size);
```
Create an arena. Pass 0 for default 64KB block size.

#### `cdsl_arena_alloc`
```c
void* cdsl_arena_alloc(cdsl_arena_t* arena, size_t size);
```
Allocate memory from the arena (8-byte aligned).

#### `cdsl_arena_strdup`
```c
char* cdsl_arena_strdup(cdsl_arena_t* arena, const char* s);
```
Duplicate a string in arena memory.

#### `cdsl_arena_free`
```c
void cdsl_arena_free(cdsl_arena_t* arena);
```
Free all arena memory at once. Does not free individual allocations.

---

## Hashmap Module

### Functions

#### `cdsl_hashmap_create`
```c
cdsl_hashmap_t* cdsl_hashmap_create(int bucket_count);
```
Create a hash map. Pass 0 for default 64 buckets.

#### `cdsl_hashmap_free`
```c
void cdsl_hashmap_free(cdsl_hashmap_t* map, cdsl_hashmap_free_fn free_fn);
```
Free a hash map and optionally destroy values with the provided callback.

#### `cdsl_hashmap_put`
```c
int cdsl_hashmap_put(cdsl_hashmap_t* map, const char* key, void* value);
```
Insert or update a key-value pair. Returns 1 if new, 0 if existing value was replaced.

#### `cdsl_hashmap_get`
```c
void* cdsl_hashmap_get(cdsl_hashmap_t* map, const char* key);
```
Look up a value by key. Returns NULL if not found.

#### `cdsl_hashmap_remove`
```c
int cdsl_hashmap_remove(cdsl_hashmap_t* map, const char* key, cdsl_hashmap_free_fn free_fn);
```
Remove an entry. If `free_fn` is not NULL, calls it with the value. Returns 1 if removed, 0 if not found.

#### `cdsl_hashmap_contains`
```c
int cdsl_hashmap_contains(cdsl_hashmap_t* map, const char* key);
```
Check if a key exists. Returns 1 if found, 0 if not.

---

## JSON Module

### Functions

#### `cdsl_json_parse`
```c
cdsl_json_value_t* cdsl_json_parse(const char* json);
```
Parse a JSON string into a value tree. Returns NULL on parse error.

#### `cdsl_json_free`
```c
void cdsl_json_free(cdsl_json_value_t* val);
```
Free a JSON value tree recursively.
