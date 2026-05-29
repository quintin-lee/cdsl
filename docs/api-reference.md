# API 参考手册

## 模块索引

| 模块 | 头文件 | 说明 |
|---|---|---|
| [AST](#ast-模块) | `ast.h` | 抽象语法树定义 |
| [Abstract](#abstract-模块) | `abstract.h` | Schema 校验 |
| [Execution](#execution-模块) | `execution.h` | VM 执行引擎 |
| [AI Bridge](#ai-bridge-模块) | `ai_bridge.h` | AI 集成 |
| [Error](#error-模块) | `cdsl_error.h` | 错误报告 |
| [Arena](#arena-模块) | `cdsl_arena.h` | 内存分配器 |
| [Hashmap](#hashmap-模块) | `cdsl_hashmap.h` | 哈希表 |
| [JSON](#json-模块) | `cdsl_json.h` | JSON 解析 |

---

## AST 模块

### 类型定义

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
    char* name;                      // Rule name
    cdsl_meta_item_t* meta_list;    // Metadata linked list
    cdsl_expr_node_t* when_expr;    // WHEN expression (simple rules)
    cdsl_action_node_t* then_action; // THEN action (simple rules)
    cdsl_metric_node_t* metrics;    // METRIC list (scoring rules)
} cdsl_rule_t;
```

### 函数

#### `cdsl_parse_string`
```c
cdsl_rule_t* cdsl_parse_string(const char* dsl_code);
```
Parse a DSL string into an AST rule. Returns NULL on error.

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
Free a rule and all its child nodes.

---

## Abstract 模块

### 类型定义

#### `cdsl_schema_t`
```c
typedef struct cdsl_schema {
    cdsl_var_schema_t* vars;       // Registered variables
    cdsl_action_schema_t* actions; // Registered actions
} cdsl_schema_t;
```

### 函数

#### `cdsl_schema_create`
```c
cdsl_schema_t* cdsl_schema_create(void);
```
Create an empty schema.

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
Register an action with its signature. Variadic args are `cdsl_type_t` values.

#### `cdsl_verify_rule`
```c
int cdsl_verify_rule(const cdsl_rule_t* rule, const cdsl_schema_t* schema,
                      char* err_buf, int err_buf_sz);
```
Verify a rule against the schema. Returns 1 if valid, 0 on error.

#### `cdsl_verify_rule_detailed`
```c
cdsl_error_list_t* cdsl_verify_rule_detailed(const cdsl_rule_t* rule,
                                               const cdsl_schema_t* schema);
```
Verify with detailed error collection. Returns error list.

---

## Execution 模块

### 类型定义

#### `cdsl_rule_status_t`
```c
typedef enum {
    CDSL_STATUS_PASSED,           // Score >= pass_threshold
    CDSL_STATUS_PARTIALLY_PASSED, // Between thresholds
    CDSL_STATUS_FAILED,           // Below threshold or critical veto
    CDSL_STATUS_ERROR             // Execution error
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

### 函数

#### `cdsl_context_create`
```c
cdsl_context_t* cdsl_context_create(const cdsl_schema_t* schema);
```
Create an empty context bound to a schema.

#### `cdsl_context_set_int` / `cdsl_context_set_float` / `cdsl_context_set_bool` / `cdsl_context_set_string`
```c
void cdsl_context_set_int(cdsl_context_t* ctx, const char* name, int val);
void cdsl_context_set_float(cdsl_context_t* ctx, const char* name, double val);
void cdsl_context_set_bool(cdsl_context_t* ctx, const char* name, int val);
void cdsl_context_set_string(cdsl_context_t* ctx, const char* name, const char* val);
```
Set a context variable value.

#### `cdsl_context_load_json`
```c
int cdsl_context_load_json(cdsl_context_t* ctx, const char* json_str);
```
Load variables from JSON. Returns 1 on success.

#### `cdsl_vm_create`
```c
cdsl_vm_t* cdsl_vm_create(const cdsl_schema_t* schema);
```
Create a virtual machine instance.

#### `cdsl_vm_register_action`
```c
void cdsl_vm_register_action(cdsl_vm_t* vm, const char* action_name, cdsl_action_cb_t cb);
```
Register an action callback.

#### `cdsl_vm_execute`
```c
cdsl_rule_report_t* cdsl_vm_execute(cdsl_vm_t* vm, const cdsl_rule_t* rule, cdsl_context_t* ctx);
```
Execute a single rule. Returns report.

#### `cdsl_report_to_json`
```c
char* cdsl_report_to_json(const cdsl_rule_report_t* report);
```
Serialize report to JSON string. Must be freed with `free()`.

#### `cdsl_ruleset_add`
```c
void cdsl_ruleset_add(cdsl_ruleset_t* set, cdsl_rule_t* rule, int priority);
```
Add a rule to a ruleset. Lower priority = executed first.

#### `cdsl_vm_execute_ruleset`
```c
cdsl_ruleset_report_t* cdsl_vm_execute_ruleset(cdsl_vm_t* vm, cdsl_ruleset_t* set, cdsl_context_t* ctx);
```
Execute all rules in a ruleset. Returns batch report.

---

## AI Bridge 模块

### 类型定义

#### `cdsl_ai_config_t`
```c
typedef struct {
    int use_mock;      // 1 = mock mode, 0 = API mode
    char* api_key;     // API key
    char* api_base;    // API base URL
    char* model;       // Model name
} cdsl_ai_config_t;
```

#### `cdsl_ai_review_t`
```c
typedef struct {
    int approved;      // 1 if passed, 0 if rejected
    int risk_score;    // 0-100 risk score
    char* reason;      // Explanation
    char* suggestions; // Improvement suggestions
} cdsl_ai_review_t;
```

### 函数

#### `cdsl_ai_config_default`
```c
cdsl_ai_config_t cdsl_ai_config_default(void);
```
Return default config with mock mode enabled.

#### `cdsl_ai_translate`
```c
char* cdsl_ai_translate(const char* natural_language,
                         const cdsl_schema_t* schema,
                         const cdsl_ai_config_t* config);
```
Translate natural language to DSL. Must be freed with `free()`.

#### `cdsl_ai_review`
```c
cdsl_ai_review_t* cdsl_ai_review(const char* dsl_code,
                                   const cdsl_schema_t* schema,
                                   const cdsl_ai_config_t* config);
```
Review DSL for safety. Must be freed with `cdsl_ai_review_free()`.

---

## Error 模块

### 类型定义

#### `cdsl_error_kind_t`
```c
typedef enum {
    CDSL_ERR_SYNTAX,   // Parse error
    CDSL_ERR_TYPE,     // Type mismatch
    CDSL_ERR_SEMANTIC, // Unknown variable/action
    CDSL_ERR_RUNTIME   // Runtime error
} cdsl_error_kind_t;
```

### 函数

#### `cdsl_error_create`
```c
cdsl_error_t* cdsl_error_create(cdsl_error_kind_t kind, int line, int column,
                                  const char* message, const char* hint);
```
Create an error instance.

#### `cdsl_error_list_create` / `cdsl_error_list_add` / `cdsl_error_list_free`
```c
cdsl_error_list_t* cdsl_error_list_create(void);
void cdsl_error_list_add(cdsl_error_list_t* list, cdsl_error_t* err);
void cdsl_error_list_free(cdsl_error_list_t* list);
```
Manage collectable error lists.

---

## Arena 模块

### 函数

#### `cdsl_arena_create`
```c
cdsl_arena_t* cdsl_arena_create(size_t block_size);
```
Create arena. Pass 0 for default 64KB blocks.

#### `cdsl_arena_alloc`
```c
void* cdsl_arena_alloc(cdsl_arena_t* arena, size_t size);
```
Allocate memory (8-byte aligned).

#### `cdsl_arena_strdup`
```c
char* cdsl_arena_strdup(cdsl_arena_t* arena, const char* s);
```
Duplicate a string in arena memory.

#### `cdsl_arena_free`
```c
void cdsl_arena_free(cdsl_arena_t* arena);
```
Free all arena memory at once.

---

## Hashmap 模块

### 函数

#### `cdsl_hashmap_create`
```c
cdsl_hashmap_t* cdsl_hashmap_create(int bucket_count);
```
Create a hash map. Pass 0 for default 64 buckets.

#### `cdsl_hashmap_put`
```c
int cdsl_hashmap_put(cdsl_hashmap_t* map, const char* key, void* value);
```
Insert or update a key-value pair.

#### `cdsl_hashmap_get`
```c
void* cdsl_hashmap_get(cdsl_hashmap_t* map, const char* key);
```
Look up a value by key. Returns NULL if not found.

#### `cdsl_hashmap_remove`
```c
int cdsl_hashmap_remove(cdsl_hashmap_t* map, const char* key, cdsl_hashmap_free_fn free_fn);
```
Remove an entry. Returns 1 if removed.

---

## JSON 模块

### 函数

#### `cdsl_json_parse`
```c
cdsl_json_value_t* cdsl_json_parse(const char* json);
```
Parse JSON string into value tree.

#### `cdsl_json_free`
```c
void cdsl_json_free(cdsl_json_value_t* val);
```
Free JSON value tree.
