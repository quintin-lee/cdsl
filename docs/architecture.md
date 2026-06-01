# Architecture Document

**Revision**: 1.0 &nbsp;·&nbsp; **Audience**: Architects & Contributors

---

## 1. Overview

C-DSL is a three-layer rule engine framework built in C23. It transforms natural language business rules into executable DSL, validates them against a formal schema, and produces structured audit reports.

### 1.1 Design Goals

| Goal                  | Description                                                        |
|-----------------------|--------------------------------------------------------------------|
| Zero dependencies     | Core library has no third-party dependencies; JSON parser is custom |
| Three-layer isolation | Syntax → Abstract → Execution: each layer has a single responsibility |
| AI-driven             | Natural language to DSL translation with optional LLM integration  |
| Multi-metric scoring  | Weighted scoring, critical-rule veto, tri-state output             |
| Easy integration      | CMake subdirectory, installable package, pkg-config                |
| Extensible            | Custom action callbacks, custom function registration, templates   |

### 1.2 Technology Stack

| Component       | Technology                  |
|-----------------|-----------------------------|
| Language        | C23                         |
| Build system    | CMake 3.14+                 |
| Lexer           | Flex 2.6+                   |
| Parser          | Bison 3.8+                  |
| Documentation   | Doxygen + Markdown          |
| Testing         | Custom lightweight framework |
| Document Parsing| LibreOfficeKit (LOK SDK)    |
| CI              | GitHub Actions              |

---

## 2. Three-Layer Architecture

```
┌──────────────────────────────────────────────────────────────┐
│                       AI Layer                                 │
│                                                                │
│  • Natural Language → DSL translation                          │
│    - Schema-aware offline generation                          │
│    - OpenAI-compatible LLM API                                │
│    - SSE streaming callbacks                                  │
│  • Rule Safety Review                                         │
│    - Structural analysis (mock mode)                          │
│    - LLM-based analysis (API mode)                            │
└──────────────────────────┬───────────────────────────────────┘
                           │ DSL String
                           ▼
┌──────────────────────────────────────────────────────────────┐
│  1. Syntax Layer                                              │
│                                                                │
│     Flex (Lexer) ──► Token Stream ──► Bison (Parser)          │
│                                            │                   │
│                                            ▼                   │
│                                      AST (Syntax Tree)         │
│                                                                │
│  Grammar constructs:                                           │
│  • RULE / META / WHEN / THEN — simple rules                   │
│  • METRIC / CASE / DEFAULT — scoring rules                    │
│  • TEMPLATE / EXTENDS — inheritance                            │
│  • AND / OR / NOT — logical operators                          │
│  • Function calls in expressions                               │
└──────────────────────────┬───────────────────────────────────┘
                           │ Raw AST (cdsl_rule_t)
                           ▼
┌──────────────────────────────────────────────────────────────┐
│  2. Schema Layer                                              │
│                                                                │
│  Schema registration and static verification:                  │
│  • Variable type checking (existence + type match)            │
│  • Action signature validation (name + arg count + types)     │
│  • Expression type compatibility (INT vs STRING, etc.)        │
│  • All errors collected (not fail-fast)                       │
│                                                                │
│  Output: Verified AST or error list                           │
└──────────────────────────┬───────────────────────────────────┘
                           │ Verified AST
                           ▼
┌──────────────────────────────────────────────────────────────┐
│  3. Execution Layer                                           │
│                                                                │
│  VM Engine:                                                   │
│  • Context binding (API or JSON)                              │
│  • AST interpretation (eval_expr)                             │
│  • Simple rules: WHEN/THEN pass-fail                          │
│  • Scoring rules: METRIC/CASE matching, aggregation           │
│  • RuleSet: priority ordering, batch execution                │
│  • Hot reload: load/remove/reload at runtime                  │
│  • Parallel: multi-threaded execution via pthreads            │
│  • Debug trace: step-by-step expression evaluation            │
│  • Compile cache: parse+verify caching by DSL hash            │
│  • Performance stats: execution counts and timing             │
│  • Code generation: DSL to C code                             │
│  • Visualization: Graphviz DOT output                         │
└──────────────────────────────────────────────────────────────┘
```

### 2.1 Syntax Layer

**Files**: `parser/lexer.l`, `parser/parser.y`, `src/ast/ast.c`, `src/ast/parse.c`, `src/ast/template.c`

The Syntax Layer is responsible for lexical analysis and parsing of DSL source text. Flex tokenizes the input into a stream of tokens; Bison consumes the token stream and constructs an abstract syntax tree (AST) using the node constructors in `ast.c`.

**Key responsibilities**:
- Tokenization of DSL keywords, identifiers, literals, and operators
- Grammar validation (syntax errors reported by Bison)
- AST construction with proper parent-child relationships
- Memory ownership: the parser allocates all AST nodes; the caller owns the returned `cdsl_rule_t`

### 2.2 Schema Layer

**Files**: `src/schema/schema.c`, `include/cdsl/util/error.h`

The Abstract Layer performs static analysis on the raw AST before execution. It verifies that the rule is semantically correct with respect to a registered schema.

**Verification checks**:
1. **Variable existence** — every identifier used in expressions must be registered in the schema
2. **Type compatibility** — binary operands must have compatible types (INT ↔ FLOAT allowed)
3. **Action signature** — action name must be registered, argument count must match, argument types must be compatible
4. **Comparison validity** — string comparison only allowed with `==` and `!=`

Two verification functions are provided:
- `cdsl_verify_rule()` — fast fail, returns first error as a string
- `cdsl_verify_rule_detailed()` — collects all errors into a structured `cdsl_error_list_t`

### 2.3 Execution Layer

**Files**: `src/vm/eval.c`, `src/vm/context.c`, `src/vm/cache.c`, `src/vm/ruleset.c`, `src/vm/codegen.c`, `src/vm/visual.c`

The Execution Layer interprets the verified AST against a runtime context. It has been modularized for better maintainability and thread-safety:

- **vm/context.c**: Manages execution contexts, variable bindings, VM lifecycle, action/function registration, execution statistics.
- **vm/eval.c**: Core AST interpreter, rule execution logic (metric and simple rules), report creation, printing, JSON serialization.
- **vm/cache.c**: Thread-safe compilation cache with robust collision handling.
- **vm/ruleset.c**: Priority-based batch execution and parallel RuleSet evaluation.
- **vm/codegen.c**: Translation of DSL rules into standalone C code.
- **vm/visual.c**: Generation of Graphviz DOT representations for rules and rulesets.

**Execution flow**:

```
1. Bind context variables via API or JSON loading
2. Walk AST nodes and evaluate expressions (eval_expr)
3. For METRIC rules:
   a. For each metric, evaluate CASE conditions in order
   b. First matching CASE determines the score and triggers its action
   c. If no CASE matches, execute DEFAULT
   d. Check is_critical → veto if failed
   e. Accumulate score
4. For simple rules:
   a. Evaluate WHEN expression
   b. If true → trigger THEN action → FAILED
   c. If false → PASSED
5. Compare total score against thresholds → tri-state status
6. Generate structured report (cdsl_rule_report_t)
```

**Additional subsystems**:
- **Compilation cache**: caches parsed+verified rules indexed by DSL source hash to avoid redundant parsing
- **Code generation**: translates DSL rules to equivalent C source code via `cdsl_codegen_rule_to_c()`
- **Visualization**: generates Graphviz DOT graphs for single rules and complete rulesets
- **Performance monitoring**: tracks execution count, metric evaluations, and timing via `cdsl_stats_t`

### 2.4 Document Parsing Layer

**Files**: `src/doc/doc_parse.cpp`, `src/doc/xml_parser.cpp`, `src/doc/xml_parser.h`, `include/cdsl/doc.h`

The document parsing layer integrates with LibreOfficeKit (LOK) to extract structured content from Word (.docx) documents for rule evaluation.

**Architecture**:
1. **LibreOfficeKit Integration** — spawns a headless LibreOffice process via `lok_cpp_init()`. The global office instance is protected by a mutex for thread-safe serialized access.
2. **FODT Export** — documents are exported to Flat ODF XML (`.fodt`) format, which contains page dimensions, paragraph styles, and text content in a single flat XML file.
3. **Custom XML Parser** — A pull-style SAX-like `XmlParser` parses the FODT XML without external dependencies. It extracts style definitions, paragraph properties, text spans, and metadata statistics.
4. **Style Inheritance** — ODF styles form `style:parent-style-name` chains. `resolve_style()` recursively walks the chain and `merge_style()` fills in missing property values from parent to child.
5. **Position Estimation** — `get_paragraph_positions()` sends `.uno:GoDown` UNO commands and captures `LOK_CALLBACK_INVALIDATE_VISIBLE_CURSOR` callbacks to estimate paragraph and text block bounding boxes in millimeters.
6. **JSON Output** — `cdsl_doc_extract_to_json()` assembles the final hierarchical JSON with pages, paragraphs, text blocks, metadata, and full text, all compatible with `cdsl_context_load_json()`.

```
Input: RULE check { WHEN user.age > 18 THEN block("adult") }

                    ┌─────────────┐
                    │   Parser    │
                    └──────┬──────┘
                           │ cdsl_rule_t (metrics == NULL)
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
Input: RULE scoring { METRIC m1 { CASE x > 10 THEN score(40) } ... }

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
         │ 40/40  │  │ 20/30  │  │  0/30* │ ← is_critical
         └────────┘  └────────┘  └────────┘
              │            │            │
              └────────────┼────────────┘
                           ▼
                    ┌──────────────────┐
                    │   Aggregator     │
                    │   + Thresholds   │ → Score: 60/100
                    │   + Critical     │ → Status: PARTIALLY PASSED
                    └──────┬───────────┘
                           ▼
                    ┌─────────────┐
                    │   Report    │
                    └─────────────┘
```

## 4. Thread Safety

| Operation                             | Safe | Notes                                    |
|---------------------------------------|------|------------------------------------------|
| `cdsl_parse_string()`                 | ✅   | Uses reentrant Flex/Bison scanner        |
| `cdsl_vm_execute()`                   | ✅   | Each thread needs its own VM instance    |
| `cdsl_context_*()`                    | ✅   | Each thread needs its own Context        |
| Schema (read-only)                    | ✅   | Can be shared across threads             |
| Rule (read-only)                      | ✅   | Parsed rules are immutable               |
| RuleSet (read-only)                   | ✅   | Safe after construction                  |
| `cdsl_vm_execute_ruleset_parallel()`  | ✅   | Creates per-thread VMs internally        |
| `cdsl_compile_cache_t`                | ✅   | Internal RWLock protection               |

---

## 5. Memory Management

| Object                     | Allocation | Free                          |
|----------------------------|------------|-------------------------------|
| `cdsl_rule_t`              | `malloc`   | `cdsl_free_rule()`            |
| `cdsl_context_t`           | `malloc`   | `cdsl_context_free()`         |
| `cdsl_vm_t`                | `malloc`   | `cdsl_vm_free()`              |
| `cdsl_rule_report_t`       | `malloc`   | `cdsl_report_free()`          |
| `cdsl_ruleset_t`           | `malloc`   | `cdsl_ruleset_free()`         |
| `cdsl_schema_t`            | `malloc`   | `cdsl_schema_free()`          |
| `cdsl_arena_t`             | `malloc`   | `cdsl_arena_free()` (bulk)    |
| `cdsl_compile_cache_t`     | `malloc`   | `cdsl_compile_cache_free()`   |
| `cdsl_ai_review_t`         | `malloc`   | `cdsl_ai_review_free()`       |
| DOT / C codegen output     | heap       | `free()`                      |
| `cdsl_ruleset_report_t`    | `malloc`   | `cdsl_ruleset_report_free()`  |

> **Note**: Internal strings in `cdsl_rule_t` are allocated by the parser and freed by `cdsl_free_rule()`. Every public `_create` function has a corresponding `_free`.

---

## 6. Error Handling Strategy

Errors are handled at two levels:

1. **String-based** (`cdsl_verify_rule`) — simple fail-fast with a human-readable message buffer. Suitable for quick validation.

2. **Structured** (`cdsl_verify_rule_detailed`) — collects all errors into a `cdsl_error_list_t` with kind, location, message, and optional hint. Suitable for IDE integration or batch reporting.

Error kinds:

| Kind                 | Meaning                          |
|----------------------|----------------------------------|
| `CDSL_ERR_SYNTAX`    | Parse error (from Bison)         |
| `CDSL_ERR_TYPE`      | Type mismatch in expression      |
| `CDSL_ERR_SEMANTIC`  | Unknown variable or action       |
| `CDSL_ERR_RUNTIME`   | Execution-time error             |

---

## 7. Build System

The project uses CMake with the following options:

| Option                     | Default | Description                           |
|----------------------------|---------|---------------------------------------|
| `CDSL_FORMAT_ON_BUILD`     | OFF     | Auto-format source before each build  |
| `CDSL_GENERATE_DOCS`       | ON      | Generate Doxygen docs on every build  |
| `BUILD_SHARED_LIBS`        | —       | Build shared library (default: both)  |

CMake targets:

| Target              | Description                             |
|---------------------|-----------------------------------------|
| `cdsl_static`       | Static library (default)                |
| `cdsl_shared`       | Shared library                          |
| `cdsl_demo`         | Demo executable                         |
| `test_ast`          | AST unit tests                          |
| `test_execution`    | Execution unit tests                    |
| `doc`               | Doxygen documentation                   |
| `format`            | Format all sources with clang-format    |
| `check-format`      | Check formatting (CI use)               |
