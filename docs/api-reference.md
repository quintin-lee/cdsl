# API Reference Manual

**Revision**: 1.0 &nbsp;·&nbsp; **Audience**: Developers

---

## Module Index

| Module                                             | Header                  | Description                         |
|----------------------------------------------------|-------------------------|-------------------------------------|
| AST (1)                                            | `<cdsl/ast.h>`          | Abstract syntax tree construction   |
| Schema (2)                                         | `<cdsl/schema.h>`       | Schema verification                 |
| Execution (3)                                      | `<cdsl/execution.h>`    | Umbrella: VM, context, reports, RuleSet |
| &emsp;Context (3a)                                 | `<cdsl/context.h>`      | Variable bindings, `cdsl_value_t`   |
| &emsp;VM (3b)                                      | `<cdsl/vm.h>`           | VM lifecycle, actions, functions    |
| &emsp;Report (3c)                                  | `<cdsl/report.h>`       | Report creation, JSON serialization |
| &emsp;Cache (3d)                                   | `<cdsl/cache.h>`        | Compilation cache                   |
| &emsp;RuleSet (3e)                                 | `<cdsl/ruleset.h>`      | Batch execution, parallel, hot reload |
| &emsp;Codegen (3f)                                 | `<cdsl/codegen.h>`      | DSL → C code generation             |
| &emsp;Visual (3g)                                  | `<cdsl/visual.h>`       | Graphviz DOT output                 |
| AI (4)                                             | `<cdsl/ai.h>`           | AI integration (mock + LLM)         |
| Error (5)                                          | `<cdsl/util/error.h>`   | Error reporting                     |
| Arena (6)                                          | `<cdsl/util/arena.h>`   | Arena memory allocator              |
| Hashmap (7)                                        | `<cdsl/util/hashmap.h>` | Hash table                          |
| JSON (8)                                           | `<cdsl/util/json.h>`    | JSON parser                         |

---

## 1. AST Module

### Type Definitions

#### `cdsl_type_t`

```c
typedef enum {
    CDSL_TYPE_INT,    /**< 32-bit signed integer  */
    CDSL_TYPE_FLOAT,  /**< 64-bit double-precision float */
    CDSL_TYPE_BOOL,   /**< Boolean (0 or non-zero) */
    CDSL_TYPE_STRING, /**< NUL-terminated string  */
    CDSL_TYPE_DATE,   /**< time_t date/datetime value */
    CDSL_TYPE_VOID    /**< No value (error or void) */
} cdsl_type_t;
```

#### `cdsl_op_t`

```c
typedef enum {
    /* Comparison operators */
    CDSL_OP_EQ, CDSL_OP_NE, CDSL_OP_LT, CDSL_OP_GT,
    CDSL_OP_LE, CDSL_OP_GE,
    /* Logical operators */
    CDSL_OP_AND, CDSL_OP_OR, CDSL_OP_NOT,
    /* Arithmetic operators */
    CDSL_OP_ADD, CDSL_OP_SUB, CDSL_OP_MUL, CDSL_OP_DIV,
    /* Unary operators */
    CDSL_OP_NEG
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
    cdsl_arena_t* arena;
} cdsl_rule_t;
```

### Functions

#### `cdsl_parse_string`

```c
cdsl_rule_t* cdsl_parse_string(const char* dsl_code);
```

Parses a DSL string into an AST rule. Returns NULL on parse failure. **Not thread-safe** (Flex uses global state).

| Returns | Condition          |
|---------|--------------------|
| Rule    | Successful parse   |
| NULL    | Parse error        |

#### `cdsl_free_rule`

```c
void cdsl_free_rule(cdsl_rule_t* rule);
```

Recursively frees a rule and all its child nodes (expressions, actions, metrics, metadata).

#### `cdsl_template_register`

```c
void cdsl_template_register(cdsl_rule_t* template_rule);
```

Registers a TEMPLATE rule for EXTENDS resolution during parsing.

#### `cdsl_template_get`

```c
cdsl_rule_t* cdsl_template_get(const char* name);
```

Looks up a registered template by name. Returns NULL if not found.

#### `cdsl_template_clear`

```c
void cdsl_template_clear(void);
```

Clears all registered templates from the global registry. Does not free the template rules.

#### `cdsl_create_extends_rule`

```c
cdsl_rule_t* cdsl_create_extends_rule(char* name, char* template_name, cdsl_meta_item_t* meta);
```

Creates a rule that inherits metrics from a registered TEMPLATE. Copies all metrics from the template into the new rule. Returns NULL if the template is not found.

#### `cdsl_meta_get`

```c
char* cdsl_meta_get(cdsl_meta_item_t* list, const char* key);
```

Finds a metadata value by key. Returns NULL if the key is not present.

---

## 2. Schema Module

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

| Function                                  | Returns     | Description                              |
|-------------------------------------------|-------------|------------------------------------------|
| `cdsl_schema_create()`                    | `schema*`   | Create an empty schema                   |
| `cdsl_schema_free(schema)`                | `void`      | Free schema and all registrations        |
| `cdsl_schema_register_var(schema, name, type)` | `void` | Register a typed variable             |
| `cdsl_schema_register_action(schema, name, ret_type, arg_count, ...)` | `void` | Register an action with variadic argument types |
| `cdsl_verify_rule(rule, schema, err_buf, err_buf_sz)` | `bool` | Fast-fail verification with string error |
| `cdsl_verify_rule_detailed(rule, schema)` | `error_list*` | Collect all errors into structured list   |

#### `cdsl_verify_rule`

```c
bool cdsl_verify_rule(const cdsl_rule_t* rule, const cdsl_schema_t* schema,
                      char* err_buf, int err_buf_sz);
```

Verifies a rule against the schema. Returns `true` if valid, `false` on error. On failure, `err_buf` contains a human-readable error message.

#### `cdsl_verify_rule_detailed`

```c
cdsl_error_list_t* cdsl_verify_rule_detailed(const cdsl_rule_t* rule,
                                              const cdsl_schema_t* schema);
```

Verifies a rule and collects all errors found (does not stop at the first error). Returns an error list that must be freed with `cdsl_error_list_free()`.

---

## 3. Execution Module

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
        time_t date_val;
    } data;
} cdsl_value_t;
```

#### `cdsl_rule_status_t`

```c
typedef enum {
    CDSL_STATUS_PASSED,           /**< Met or exceeded pass_threshold */
    CDSL_STATUS_PARTIALLY_PASSED, /**< Met partial_threshold but not pass_threshold */
    CDSL_STATUS_FAILED,           /**< Below all thresholds or critical metric failed */
    CDSL_STATUS_ERROR             /**< Execution error */
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
    long total_executions;        /**< Number of VM execute() calls */
    long total_rules_executed;    /**< Number of individual rule evaluations */
    long total_metrics_evaluated; /**< Number of metric evaluations */
    long total_actions_triggered; /**< Number of action callbacks invoked */
    double total_time_us;         /**< Total execution time in microseconds */
    double avg_time_us;           /**< Average time per execution */
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
    cdsl_hashmap_t* map;
    pthread_rwlock_t lock;
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

| Function                                               | Description                                      |
|--------------------------------------------------------|--------------------------------------------------|
| `cdsl_context_create(schema)`                          | Create empty context bound to a schema           |
| `cdsl_context_free(ctx)`                               | Free context and all variable bindings           |
| `cdsl_context_set_int(ctx, name, val)`                 | Set or update an integer variable                |
| `cdsl_context_set_float(ctx, name, val)`               | Set or update a float variable                   |
| `cdsl_context_set_bool(ctx, name, val)`                | Set or update a boolean variable                 |
| `cdsl_context_set_string(ctx, name, val)`              | Set or update a string variable                  |
| `cdsl_context_set_date(ctx, name, val)`                | Set or update a date/time variable               |
| `cdsl_context_get_int(ctx, name, default_val)`         | Get integer value (returns default if not found) |
| `cdsl_context_get_float(ctx, name, default_val)`       | Get float value (returns default if not found)   |
| `cdsl_context_get_bool(ctx, name, default_val)`        | Get boolean value (returns default if not found) |
| `cdsl_context_get_string(ctx, name, default_val)`      | Get string value (returns default if not found)  |
| `cdsl_context_get_date(ctx, name, default_val)`        | Get date value (returns default if not found)    |
| `cdsl_context_remove(ctx, name)`                       | Remove a variable (returns 1 if found, 0 if not) |
| `cdsl_context_load_json(ctx, json_str)`                | Load variables from JSON (returns 1 on success)  |

#### `cdsl_context_load_json`

```c
int cdsl_context_load_json(cdsl_context_t* ctx, const char* json_str);
```

Loads variables from a JSON string. Nested objects are flattened with dot notation (e.g., `{"user":{"age":25}}` → `user.age`). Returns 1 on success, 0 on parse failure.

### Virtual Machine

| Function                                       | Description                                      |
|------------------------------------------------|--------------------------------------------------|
| `cdsl_vm_create(schema)`                       | Create a VM instance (one per thread)            |
| `cdsl_vm_free(vm)`                             | Free VM and registered callbacks                 |
| `cdsl_vm_register_action(vm, name, cb)`        | Register an action callback                      |
| `cdsl_vm_register_function(vm, name, cb)`      | Register a function callback for expressions     |
| `cdsl_vm_set_debug(vm, enabled)`               | Enable/disable debug trace to stderr             |
| `cdsl_vm_get_max_expr_depth(vm)`               | Get max expression nesting depth                 |
| `cdsl_vm_set_max_expr_depth(vm, depth)`        | Set max expression nesting depth (must be > 0)   |
| `cdsl_vm_get_stats(vm)`                        | Get execution statistics (caller must free)      |
| `cdsl_vm_reset_stats(vm)`                      | Reset all statistics counters                    |

### Rule Execution

| Function                                                       | Description                                         |
|----------------------------------------------------------------|-----------------------------------------------------|
| `cdsl_vm_execute(vm, rule, ctx)`                               | Execute a single rule, return report                |
| `cdsl_report_free(report)`                                     | Free a rule execution report                        |
| `cdsl_report_print(report)`                                    | Print human-readable report to stdout               |
| `cdsl_report_to_json(report)`                                  | Serialize report to JSON string (caller frees)      |

### Compilation Cache

| Function                                                        | Description                                        |
|-----------------------------------------------------------------|----------------------------------------------------|
| `cdsl_compile_cache_create(capacity)`                           | Create cache (0 = default 64 slots)                |
| `cdsl_compile_cache_free(cache)`                                | Free cache and compiled rules                      |
| `cdsl_compile(cache, dsl_code, schema, err_buf, sz)`            | Parse + verify + cache; returns compiled rule      |
| `cdsl_vm_execute_compiled(vm, compiled, ctx)`                   | Execute a cached compiled rule                     |

### Code Generation

| Function                                                        | Description                                        |
|-----------------------------------------------------------------|----------------------------------------------------|
| `cdsl_codegen_rule_to_c(rule, schema)`                          | Generate C source from rule (caller frees)         |
| `cdsl_codegen_to_file(rule, schema, filepath)`                  | Write generated C to file (returns 1 on success)   |

### Visualization

| Function                                                        | Description                                        |
|-----------------------------------------------------------------|----------------------------------------------------|
| `cdsl_rule_to_dot(rule)`                                        | Generate DOT graph for a single rule               |
| `cdsl_rule_to_dot_file(rule, filepath)`                         | Write single-rule DOT to file                      |
| `cdsl_ruleset_to_dot(set)`                                      | Generate DOT graph for a ruleset with dependencies |
| `cdsl_ruleset_to_dot_file(set, filepath)`                       | Write ruleset DOT to file                          |

### RuleSet Management

| Function                                                                  | Description                                      |
|---------------------------------------------------------------------------|--------------------------------------------------|
| `cdsl_ruleset_create()`                                                   | Create empty ruleset                             |
| `cdsl_ruleset_free(set)`                                                  | Free ruleset and all contained rules                   |
| `cdsl_ruleset_add(set, rule, priority)`                                   | Add rule with priority (lower = earlier)         |
| `cdsl_ruleset_remove(set, rule_name)`                                     | Remove rule by name (returns 1 if found)         |
| `cdsl_vm_execute_ruleset(vm, set, ctx)`                                   | Execute all rules in priority order              |
| `cdsl_vm_execute_ruleset_parallel(vm, set, ctx, thread_count)`            | Execute rules in parallel (0 = default 4)        |
| `cdsl_ruleset_report_free(report)`                                        | Free batch report                                |
| `cdsl_ruleset_report_print(report)`                                       | Print batch report to stdout                     |
| `cdsl_ruleset_load_file(set, filepath, priority, schema, err_buf, sz)`    | Parse DSL file and add to ruleset                |
| `cdsl_ruleset_load_string(set, dsl, priority, schema, err_buf, sz)`       | Parse DSL string and add to ruleset              |
| `cdsl_ruleset_reload_file(set, rule_name, filepath, schema, err_buf, sz)` | Replace rule by re-reading file                  |
| `cdsl_ruleset_validate_deps(set, err_buf, sz)`                            | Validate depends_on references (returns 1 if OK) |
| `cdsl_ruleset_topo_sort(set)`                                              | Topologically sort by depends_on                 |

---

## 4. AI Module

### Type Definitions

#### `cdsl_ai_config_t`

```c
typedef struct {
    int use_mock;           /**< 1 = offline generation, 0 = LLM API */
    char* api_key;          /**< API key for LLM service */
    char* api_base;         /**< API base URL (e.g. "https://api.openai.com/v1") */
    char* model;            /**< Model name (e.g. "gpt-4o-mini") */
    char* business_context; /**< Optional business info to guide DSL generation */
    cdsl_ai_cache_t* cache; /**< Optional external cache implementation */
    char* provider_name;    /**< Provider name (e.g. "default", "langchain") */
    char* cache_driver_name;/**< Global cache driver name (e.g. "redis") */
} cdsl_ai_config_t;
```

#### `cdsl_ai_review_t`

```c
typedef struct {
    int approved;       /**< 1 if rule passes safety review */
    int risk_score;     /**< 0-100 risk score (higher = riskier) */
    char* reason;       /**< Review explanation */
    char* suggestions;  /**< Improvement suggestions */
} cdsl_ai_review_t;
```

#### External Cache Interface

```c
typedef struct {
    void* ctx;
    char* (*get)(void* ctx, const char* key);
    void (*put)(void* ctx, const char* key, const char* value);
} cdsl_ai_cache_t;
```

### External Provider Interface

```c
typedef struct {
    void* ctx;
    char* (*translate)(void* ctx, const char* nl,
                       const cdsl_schema_t* schema,
                       const cdsl_ai_config_t* cfg);
    cdsl_ai_review_t* (*review)(void* ctx, const char* dsl,
                                const cdsl_schema_t* schema,
                                const cdsl_ai_config_t* cfg);
    char* (*translate_stream)(void* ctx, const char* nl,
                              const cdsl_schema_t* schema,
                              const cdsl_ai_config_t* cfg,
                              void (*callback)(const char*, void*),
                              void* user_data);
    char* (*review_stream)(void* ctx, const char* dsl,
                           const cdsl_schema_t* schema,
                           const cdsl_ai_config_t* cfg,
                           void (*callback)(const char*, void*),
                           void* user_data);
} cdsl_ai_provider_t;
```

### Callback Type

```c
typedef void (*cdsl_ai_stream_cb_t)(const char* chunk, void* user_data);
```

### Functions

| Function                                                          | Description                                        |
|-------------------------------------------------------------------|----------------------------------------------------|
| `cdsl_ai_config_default()`                                        | Return config with mock mode enabled               |
| `cdsl_ai_register_provider(name, provider)`                       | Register a custom AI provider by name              |
| `cdsl_ai_register_cache_driver(name, cache)`                      | Register a custom cache driver by name             |
| `cdsl_ai_translate(nl, schema, config)`                           | NL to DSL translation (caller frees result)        |
| `cdsl_ai_review(dsl_code, schema, config)`                        | DSL safety review (caller frees result)            |
| `cdsl_ai_review_free(review)`                                     | Free review result                                 |
| `cdsl_ai_translate_stream(nl, schema, config, cb, user_data)`     | Streaming translation with per-chunk callback      |
| `cdsl_ai_review_stream(dsl_code, schema, config, cb, user_data)`  | Streaming review with per-chunk callback           |

---

## 5. Error Module

### Type Definitions

```c
typedef enum {
    CDSL_ERR_SYNTAX,   /**< Parse error */
    CDSL_ERR_TYPE,     /**< Type mismatch */
    CDSL_ERR_SEMANTIC, /**< Unknown variable/action */
    CDSL_ERR_RUNTIME   /**< Execution-time error */
} cdsl_error_kind_t;

typedef struct {
    cdsl_error_kind_t kind;
    int line;
    int column;
    char* message;
    char* hint;
} cdsl_error_t;

typedef struct {
    cdsl_error_t** errors;
    int count;
    int capacity;
} cdsl_error_list_t;
```

### Functions

| Function                                         | Description                               |
|--------------------------------------------------|-------------------------------------------|
| `cdsl_error_create(kind, line, col, msg, hint)`  | Create structured error (copies strings)  |
| `cdsl_error_free(err)`                           | Free single error                         |
| `cdsl_error_print(err)`                          | Print error to stderr in formatted style  |
| `cdsl_error_list_create()`                       | Create empty error list                   |
| `cdsl_error_list_add(list, err)`                 | Add error to list (takes ownership)       |
| `cdsl_error_list_free(list)`                     | Free list and all contained errors        |
| `cdsl_error_list_has_errors(list)`               | Returns 1 if list contains errors         |
| `cdsl_error_list_print(list)`                    | Print all errors in list                  |

---

## 6. Arena Module

| Function                           | Description                                   |
|------------------------------------|-----------------------------------------------|
| `cdsl_arena_create(block_size)`    | Create arena (0 = default 64KB block size)    |
| `cdsl_arena_alloc(arena, size)`    | Allocate 8-byte aligned memory from arena     |
| `cdsl_arena_strdup(arena, s)`      | Duplicate string in arena memory              |
| `cdsl_arena_free(arena)`           | Free all arena memory at once (bulk release)  |

---

## 7. Hashmap Module

| Function                                         | Description                                   |
|--------------------------------------------------|-----------------------------------------------|
| `cdsl_hashmap_create(bucket_count)`              | Create hash map (0 = default 64 buckets)      |
| `cdsl_hashmap_free(map, free_fn)`                | Free map; optional destructor for values      |
| `cdsl_hashmap_put(map, key, value)`              | Insert or update; returns 1 on success        |
| `cdsl_hashmap_get(map, key)`                     | Look up by key; returns NULL if not found     |
| `cdsl_hashmap_remove(map, key, free_fn)`         | Remove entry; returns 1 if found, 0 otherwise |
| `cdsl_hashmap_has(map, key)`                     | Check key existence; returns 1 if found       |
| `cdsl_hashmap_iterate(map, cb, user_data)`       | Iterate over all entries calling cb for each  |
| `cdsl_hashmap_keys(map, count)`                  | Get all keys as NULL-terminated array         |

---

## 8. JSON Module

| Function                       | Description                                    |
|--------------------------------|------------------------------------------------|
| `cdsl_json_parse(json)`        | Parse JSON string into value tree              |
| `cdsl_json_free(val)`          | Recursively free JSON value tree               |
| `cdsl_json_object_length(val)` | Return number of keys in JSON object           |
| `cdsl_json_array_length(val)`  | Return number of elements in JSON array        |
| `cdsl_json_get_object(val, k)` | Get child object by key (NULL if not found)    |
| `cdsl_json_get_array(val, i)`  | Get array element by index (NULL if out of range)|
