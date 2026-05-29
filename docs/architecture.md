# C-DSL Architecture Document

## 1. Overview

C-DSL is a C-based DSL rule engine framework for business rule validation. Supports natural language rule descriptions → DSL translation, AI safety review, multi-metric scoring, and tri-state audit results.

### 1.1 Design Goals

| Goal | Description |
|---|---|
| **Zero external dependencies** | Core library has no third-party deps; JSON parser is custom |
| **Three-layer architecture** | Syntax → Abstract → Execution, clear separation of concerns |
| **AI driven** | Natural language to DSL translation, AI rule safety review |
| **Multi-metric scoring** | Weighted scoring, critical-rule veto, tri-state output |
| **Easy integration** | CMake integration, static/shared library, pkg-config |
| **Extensible** | Custom action callbacks, custom function registration, rule templates |

### 1.2 Tech Stack

- **Language**: C99
- **Build**: CMake 3.14+
- **Parsing**: Flex 2.6+ / Bison 3.8+
- **Documentation**: Doxygen
- **Testing**: Custom lightweight test framework

---

## 2. Three-Layer Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                     AI Bridge Layer                           │
│         Natural Language → DSL Translation                    │
│         Rule Safety Review (mock / LLM / streaming)          │
└────────────────────────────┬─────────────────────────────────┘
                             │ DSL String
                             ▼
┌──────────────────────────────────────────────────────────────┐
│  1. Syntax Layer (Syntax Layer)                               │
│     Flex (Lexer) ──► Token Stream ──► Bison (Parser)         │
│                                            │                  │
│                                            ▼                  │
│                                      AST (Syntax Tree)        │
│     Grammar: simple WHEN/THEN, METRIC/CASE/DEFAULT,           │
│              TEMPLATE/EXTENDS inheritance                     │
└──────────────────────────────┬───────────────────────────────┘
                               │ Raw AST
                               ▼
┌──────────────────────────────────────────────────────────────┐
│  2. Abstract Layer (Abstract Layer)                           │
│     Schema Registration                                       │
│     ├─ Variable type checking                                 │
│     ├─ Action signature validation                            │
│     └─ Expression type compatibility                          │
│     (Guarantees variable existence, type safety,              │
│      prevents runtime out-of-bounds)                          │
└──────────────────────────────┬───────────────────────────────┘
                               │ Verified AST
                               ▼
┌──────────────────────────────────────────────────────────────┐
│  3. Execution Layer (Execution Layer)                          │
│     VM Engine                                                 │
│     ├─ Context binding (API or JSON)                          │
│     ├─ AST interpretation (eval_expr)                         │
│     ├─ Simple rules: WHEN/THEN pass-fail                      │
│     ├─ Scoring rules: METRIC/CASE matching, aggregation       │
│     ├─ RuleSet: priority ordering, batch execution            │
│     ├─ Hot reload: load/remove/reload rules at runtime        │
│     ├─ Parallel: multi-threaded RuleSet execution             │
│     ├─ Debug trace: step-by-step expression evaluation        │
│     ├─ Compile cache: parse+verify caching by DSL hash        │
│     ├─ Performance stats: execution counts and timing         │
│     ├─ Code generation: DSL to C code                         │
│     └─ Visualization: Graphviz DOT output                     │
└──────────────────────────────────────────────────────────────┘
```

### 2.1 Syntax Layer

**Responsibility**: Parse DSL source text into an abstract syntax tree (AST).

**Components**:
- `parser/lexer.l` — Flex lexer, tokenizes source into token stream
- `parser/parser.y` — Bison parser, builds AST from token stream
- `src/ast.c` — AST node construction and memory management

**Grammar keywords**:
- `RULE` / `META` / `WHEN` / `THEN` — simple rules
- `METRIC` / `CASE` / `DEFAULT` — scoring rules
- `TEMPLATE` / `EXTENDS` — template inheritance
- `AND` / `OR` / `NOT` — logical operators

### 2.2 Abstract Layer

**Responsibility**: Static analysis of AST — type safety and semantic correctness.

**Components**:
- `src/abstract.c` — Type checker and semantic validator
- `include/cdsl_error.h` — Structured error reporting

**Checks performed**:
- Variable existence (lookup in Schema)
- Operand type compatibility (e.g., INT vs STRING comparison)
- Action argument count and types
- All errors collected (not fail-fast)

### 2.3 Execution Layer

**Responsibility**: Interpret the AST, bind runtime context, generate evaluation reports.

**Components**:
- `src/execution.c` — AST interpreter, context management, report generation, RuleSet, compilation cache, code generation, visualization
- `src/cdsl_json.c` — JSON context loader

**Execution flow**:
```
1. Bind context variables (cdsl_context_t)
2. Walk AST nodes and evaluate (eval_expr)
3. For METRIC rules: match CASE per metric, accumulate score
4. For simple rules: evaluate WHEN, trigger THEN on true
5. Score vs thresholds → tri-state result
6. Generate structured report (cdsl_rule_report_t)
```

**Additional capabilities**:
- **Debug trace**: When `cdsl_vm_set_debug(vm, 1)` is called, each expression evaluation prints its type and value to stderr
- **Hot reload**: Rules can be loaded from files, removed by name, and reloaded without restart
- **Parallel**: `cdsl_vm_execute_ruleset_parallel()` distributes rules across threads
- **Compile cache**: `cdsl_compile()` returns a cached parsed+verified rule; `cdsl_vm_execute_compiled()` executes it
- **Stats**: `cdsl_vm_get_stats()` returns execution count and timing data
- **Code generation**: `cdsl_codegen_rule_to_c()` outputs C source code equivalent to a rule
- **Visualization**: `cdsl_rule_to_dot()` / `cdsl_ruleset_to_dot()` generate Graphviz DOT format

---

## 3. Data Flow

### 3.1 Simple Rule

```
Input: "RULE check { WHEN user.age > 18 THEN block(\"adult\") }"
                          │
                          ▼
                    ┌─────────────┐
                    │   Parser    │
                    └──────┬──────┘
                           │ cdsl_rule_t
                           ▼
                    ┌─────────────┐
                    │  Verifier   │ ← Schema
                    └──────┬──────┘
                           │ Verified AST
                           ▼
                    ┌─────────────┐
                    │     VM      │ ← Context {user.age: 25}
                    └──────┬──────┘
                           │
                           ▼
                    ┌─────────────┐
                    │   Report    │ → PASSED / FAILED
                    └─────────────┘
```

### 3.2 Scoring Rule

```
Input: RULE scoring { METRIC m1 { CASE ... THEN score(40) } METRIC m2 { ... } }
                          │
                          ▼
                    ┌─────────────┐
                    │   Parser    │
                    └──────┬──────┘
                           │ cdsl_rule_t (metrics != NULL)
                           ▼
                    ┌─────────────┐
                    │  Verifier   │
                    └──────┬──────┘
                           ▼
                    ┌─────────────┐
                    │     VM      │
                    └──────┬──────┘
                           │
              ┌────────────┼────────────┐
              ▼            ▼            ▼
         ┌────────┐  ┌────────┐  ┌────────┐
         │ METRIC │  │ METRIC │  │ METRIC │
         │  m1    │  │  m2    │  │  m3    │
         │ 40/40  │  │ 20/30  │  │ 0/30 * │ ← is_critical
         └────────┘  └────────┘  └────────┘
              │            │            │
              └────────────┼────────────┘
                           ▼
                    ┌─────────────┐
                    │ Aggregator  │ → Score: 60/100
                    │ + Threshold │ → Status: PARTIALLY PASSED
                    │ + Critical  │
                    └──────┬──────┘
                           ▼
                    ┌─────────────┐
                    │   Report    │
                    └─────────────┘
```

---

## 4. Thread Safety

| Operation | Thread Safe | Notes |
|-----------|-------------|-------|
| `cdsl_parse_string()` | ❌ | Flex/Bison use global state; serialize or use in single thread |
| `cdsl_vm_execute()` | ✅ | Each thread needs its own VM instance |
| `cdsl_context_*()` | ✅ | Each thread needs its own Context |
| `cdsl_schema_t` (read-only) | ✅ | Schema can be shared across threads |
| `cdsl_rule_t` (read-only) | ✅ | Parsed rules can be shared across threads |
| `cdsl_ruleset_t` (read-only) | ✅ | RuleSet can be shared after construction |
| `cdsl_vm_execute_ruleset_parallel()` | ✅ | Creates internal per-thread VMs |
| `cdsl_compile_cache_t` | ❌ | Cache not thread-safe; protect with mutex |

---

## 5. Memory Management

| Object | Allocation | Free |
|--------|-----------|------|
| `cdsl_rule_t` | `malloc` | `cdsl_free_rule()` |
| `cdsl_context_t` | `malloc` | `cdsl_context_free()` |
| `cdsl_vm_t` | `malloc` | `cdsl_vm_free()` |
| `cdsl_report_t` | `malloc` | `cdsl_report_free()` |
| `cdsl_ruleset_t` | `malloc` | `cdsl_ruleset_free()` |
| `cdsl_schema_t` | `malloc` | `cdsl_schema_free()` |
| `cdsl_arena_t` | `malloc` | `cdsl_arena_free()` (bulk release) |
| `cdsl_compile_cache_t` | `malloc` | `cdsl_compile_cache_free()` |
| `cdsl_ai_review_t` | `malloc` | `cdsl_ai_review_free()` |
| DOT/C codegen output | `malloc` (via `open_memstream`) | `free()` |
| `cdsl_ruleset_report_t` | `malloc` | `cdsl_ruleset_report_free()` |

**Note**: Internal strings in `cdsl_rule_t` from `cdsl_parse_string()` are copied by the parser; free with `cdsl_free_rule()`.
