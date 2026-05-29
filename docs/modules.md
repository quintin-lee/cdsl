# Module Design Document

**Revision**: 1.0 &nbsp;·&nbsp; **Audience**: Contributors

---

## Module Overview

```
cdsl/
├── Core Modules
│   ├── ast          — Abstract syntax tree construction
│   ├── abstract     — Schema verification and type checking
│   ├── execution    — VM engine, context, reports, RuleSet, codegen, visualization
│   └── ai_bridge    — AI translation and safety review
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
    ├── cmake/        — Package config templates
    └── docs/         — Markdown documentation
```

---

## 1. AST Module (`ast.h` / `ast.c`)

### Responsibility

Defines all DSL syntax node types and provides constructors/free functions for the abstract syntax tree (AST). The AST is the output of the parser and the input to verification and execution.

### Data Structure

```
cdsl_rule_t
├── name                   — Rule identifier string
├── meta_list              — cdsl_meta_item_t linked list
│   └── { key, value, next }
├── when_expr              — cdsl_expr_node_t (NULL for metric rules)
├── then_action            — cdsl_action_node_t (NULL for metric rules)
├── metrics                — cdsl_metric_node_t linked list (NULL for simple rules)
│   └── cdsl_metric_node_t
│       ├── name
│       ├── meta_list
│       ├── case_list      — cdsl_case_node_t linked list
│       │   └── cdsl_case_node_t
│       │       ├── condition  — cdsl_expr_node_t
│       │       └── action     — cdsl_action_node_t
│       └── default_action — cdsl_action_node_t
└── template_name          — Name of extended template (or NULL)
```

### Expression Types

| Enum                 | Data Field       | Example         |
|----------------------|------------------|-----------------|
| `CDSL_EXPR_ID`       | `string`         | `user.age`      |
| `CDSL_EXPR_INT`      | `int`            | `42`            |
| `CDSL_EXPR_FLOAT`    | `double`         | `3.14`          |
| `CDSL_EXPR_BOOL`     | `int`            | `true`          |
| `CDSL_EXPR_STRING`   | `string`         | `"hello"`       |
| `CDSL_EXPR_BINARY`   | `op + left + right` | `a > b`      |
| `CDSL_EXPR_UNARY`    | `op + expr`      | `!flag`         |
| `CDSL_EXPR_CALL`     | `name + args`    | `strlen(x)`     |

### Parser Integration

The parser (`parser.y`) calls AST constructors (e.g., `cdsl_create_expr_binary()`) as it reduces grammar rules. The `cdsl_parse_string()` function wraps the Flex/Bison pipeline:

```
dsl_string → yy_scan_string() → yyparse() → final_parsed_rule
```

### Template Registration

Templates are stored in a global linked list within `ast.c`. When a `RULE ... EXTENDS` is parsed, `cdsl_create_extends_rule()` copies metrics from the registered template into the new rule via a deep copy of each metric's metadata and case structure (action/expression pointers are shared, not deep-copied).

---

## 2. Abstract Module (`abstract.h` / `abstract.c`)

### Responsibility

Schema registration and static semantic verification of AST rules before execution. Catches type errors, undefined variables, and invalid action signatures.

### Schema Structure

```
cdsl_schema_t
├── vars     — cdsl_var_schema_t linked list
│   └── { name: "user.age", type: CDSL_TYPE_INT, next }
└── actions  — cdsl_action_schema_t linked list
    └── { name: "block", return_type: VOID, arg_count: 1, arg_types: [STRING], next }
```

### Verification Flow

```
cdsl_verify_rule(rule, schema)
    │
    ├── If rule uses metrics:
    │   For each metric:
    │     For each case:
    │       resolve_expr_type(condition) → checks variable existence + type compatibility
    │       verify_action(action) → checks name, arg count, arg types
    │     verify_action(default_action)
    │
    └── If simple rule:
        resolve_expr_type(when_expr)
        verify_action(then_action)
```

### Type Resolution Rules

| Operation                  | Condition                  | Result type |
|----------------------------|----------------------------|-------------|
| Literal (INT/FLOAT/BOOL/STRING) | —                      | Its type    |
| Variable lookup (ID)       | Must exist in schema       | Schema type |
| Binary comparison (==, !=, <, >, <=, >=) | Compatible operands | BOOL |
| Logical AND / OR           | —                          | BOOL        |
| String comparison          | Only == and != allowed     | BOOL        |
| Type promotion             | INT + FLOAT → FLOAT        | FLOAT       |
| Type mismatch              | INT vs STRING              | ERROR       |

### Error Collection

Two verification modes:

| Function                  | Behavior                           | Use case              |
|---------------------------|------------------------------------|-----------------------|
| `cdsl_verify_rule()`      | Fail-fast, first error as string   | Quick validation      |
| `cdsl_verify_rule_detailed()` | Collect all errors into list    | IDE / batch reporting |

---

## 3. Execution Module (`execution.h` / `execution.c`)

### Responsibility

The largest module. Handles: runtime context binding, AST interpretation, action dispatch, report generation, RuleSet management, compilation caching, code generation, and Graphviz visualization.

### Context (`cdsl_context_t`)

```
cdsl_context_t
├── schema   — Pointer to associated schema (read-only)
└── entries  — cdsl_context_entry_t linked list
    ├── { name: "user.age",  value: {INT, 25} }
    ├── { name: "user.name", value: {STRING, "Alice"} }
    └── ...
```

Two binding methods:

1. **API binding**: type-specific setters (`cdsl_context_set_int`, etc.)
2. **JSON loading**: `cdsl_context_load_json()` parses JSON and recursively binds variables with dot-notation keys

### Expression Evaluation (`eval_expr`)

```
eval_expr(expr, ctx, vm, debug)
    │
    ├── CDSL_EXPR_INT      → return typed value
    ├── CDSL_EXPR_FLOAT    → return typed value
    ├── CDSL_EXPR_BOOL     → return typed value
    ├── CDSL_EXPR_STRING   → return typed value
    ├── CDSL_EXPR_ID       → ctx_get() lookup
    ├── CDSL_EXPR_UNARY    → recurse + apply NOT
    ├── CDSL_EXPR_BINARY   → recurse left/right + apply operator
    │   ├── AND/OR → short-circuit evaluation
    │   └── Comparison → numeric or string comparison
    └── CDSL_EXPR_CALL     → lookup registered function + invoke callback
```

### Tri-state Decision Logic

```
if (any is_critical metric failed):
    status = FAILED (veto)
else:
    total = sum of all metric scores
    if (total >= pass_threshold):      → PASSED
    else if (total >= partial_threshold): → PARTIALLY_PASSED
    else:                              → FAILED
```

### RuleSet Batch Execution

```
cdsl_ruleset_t
├── entries — priority-sorted linked list
│   ├── { rule: r1, priority: 1 }
│   ├── { rule: r2, priority: 5 }
│   └── { rule: r3, priority: 10 }
└── count: 3
```

Rules are maintained in ascending priority order. `cdsl_vm_execute_ruleset()` iterates through entries and aggregates per-rule reports.

### Compilation Cache

The cache is a hash table indexed by the DSL source string (djb2 hash). On cache hit, parsing and verification are skipped:

```
cdsl_compile(cache, dsl_string, schema)
    │
    ├── hash = hash_dsl_string(dsl_string)
    ├── if cache[hash] matches → return cached rule
    ├── else:
    │   ├── cdsl_parse_string(dsl_string) → rule
    │   ├── cdsl_verify_rule(rule, schema) → verify
    │   └── cache[hash] = rule
    └── return rule
```

### Code Generation (`cdsl_codegen_rule_to_c`)

Translates DSL rules to equivalent C source code:

- Metric rules → C function with `get_int` callbacks and `goto` dispatch
- Simple rules → C function with `if (condition) { action(); return 0; }`

### Visualization (`cdsl_rule_to_dot`)

Generates Graphviz DOT format:

- Variables → blue ellipses
- Conditions → green diamonds
- Functions → pink boxes
- Literals → yellow boxes
- Critical metrics → salmon boxes

---

## 4. AI Bridge Module (`ai_bridge.h` / `ai_bridge.c`)

### Responsibility

Natural language to DSL translation and DSL rule safety review, with offline (mock) and online (LLM API) modes.

### Work Modes

| Mode   | `use_mock` | Mechanism              | Description                     |
|--------|------------|------------------------|----------------------------------|
| Offline | 1          | Schema-based generation | Creates rules from schema vars   |
| API     | 0          | cURL + LLM API         | OpenAI-compatible HTTP calls     |
| Stream  | 0 + stream | SSE callbacks           | Streaming chunk delivery         |

### Offline Generation Strategy

When no API is configured, the bridge generates DSL rules dynamically:

1. If input contains "when" or "if" keywords → simple WHEN/THEN rule
2. Otherwise → multi-metric scoring rule with one metric per schema variable
3. Each metric uses a type-appropriate CASE condition (e.g., `>= 0` for INT, `!= ""` for STRING)
4. Rule name is extracted from the first word of natural language input

### Review Scoring (Mock Mode)

| Check                 | Points | Description                |
|-----------------------|--------|----------------------------|
| META block            | +15    | Has metadata block         |
| METRIC block          | +15    | Uses multi-metric structure|
| CASE + DEFAULT        | +15    | Has complete branches      |
| is_critical           | +10    | Has critical metric marker |
| score() usage         | +10    | Has score function calls   |

Score ≥ 40 → approved.

### API Integration

The bridge constructs a cURL command to call an OpenAI-compatible chat completions endpoint:

- Non-streaming: `curl -s -X POST`
- Streaming: `curl -s -N` (SSE mode) with per-chunk callback invocation

Response parsing extracts the `content` field from the JSON response and optionally extracts DSL from ` ```dsl ` code blocks.

---

## 5. Infrastructure Modules

### 5.1 JSON Parser (`cdsl_json.h` / `cdsl_json.c`)

A zero-dependency, recursive-descent JSON parser supporting:

- Objects (`{}`), arrays (`[]`), strings, numbers, booleans, null
- Nested structures (arbitrary depth)
- Used by `cdsl_context_load_json()` to bind context variables

The parser does NOT handle escape sequences (e.g., `\n`, `\"`) — these are passed through literally.

### 5.2 Error Reporting (`cdsl_error.h` / `cdsl_error.c`)

Structured error types with hint support:

| Kind                 | Meaning                    |
|----------------------|----------------------------|
| `CDSL_ERR_SYNTAX`    | Parse error                |
| `CDSL_ERR_TYPE`      | Type mismatch              |
| `CDSL_ERR_SEMANTIC`  | Unknown variable/action    |
| `CDSL_ERR_RUNTIME`   | Execution error            |

Error lists are dynamically allocated (initial capacity 16, doubles as needed).

### 5.3 Arena Allocator (`cdsl_arena.h` / `cdsl_arena.c`)

A bump allocator for same-lifetime objects:

- 8-byte aligned allocations
- Default 64 KB block size
- New blocks allocated on demand (when current block is exhausted)
- One-shot `free()` releases all memory at once
- Ideal for AST node allocation during parsing

### 5.4 Hash Table (`cdsl_hashmap.h` / `cdsl_hashmap.c`)

A separate-chaining hash table with djb2 hashing:

- String keys, generic `void*` values
- O(1) average lookup
- Optional destructor callback per entry for value cleanup
- Used by: template registry, compile cache
- Not thread-safe
