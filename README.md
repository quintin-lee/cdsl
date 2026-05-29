# C-DSL Rule Engine

[![CI](https://github.com/quintin-lee/cdsl/actions/workflows/ci.yml/badge.svg)](https://github.com/quintin-lee/cdsl/actions/workflows/ci.yml)
[![Docs](https://img.shields.io/badge/docs-github--pages-blue)](https://quintin-lee.github.io/cdsl/)
[![License: MIT](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![C Standard](https://img.shields.io/badge/C-99-blue)](https://en.wikipedia.org/wiki/C99)

**C-DSL** is an AI-powered domain-specific language rule engine for business rule validation. It translates natural language rules into executable DSL, evaluates them with multi-metric scoring, and produces tri-state audit reports (PASSED / PARTIALLY PASSED / FAILED).

---

## Table of Contents

- [Features](#features)
- [Quick Start](#quick-start)
- [Integration](#integration)
- [Architecture Overview](#architecture-overview)
- [DSL Syntax](#dsl-syntax)
- [Project Structure](#project-structure)
- [API Overview](#api-overview)
- [Documentation](#documentation)
- [Thread Safety](#thread-safety)
- [License](#license)

---

## Features

### Core Engine

- **Three-layer architecture** — Syntax (Flex/Bison) → Abstract (Schema verification) → Execution (VM)
- **Dual rule types** — Simple WHEN/THEN pass-fail and multi-metric METRIC/CASE/DEFAULT scoring
- **Tri-state results** — PASSED / PARTIALLY PASSED / FAILED with weighted scoring and critical-rule veto
- **Zero external dependencies** — Custom JSON parser, arena allocator, hash map; no third-party libraries

### Rule Management

- **RuleSet batch execution** — Priority-ordered rule groups with aggregate reports
- **Hot reload** — Load, remove, and reload rules at runtime from files or strings
- **Dependency & topology** — `depends_on` metadata with topological sort for ordered execution
- **Parallel execution** — Multi-threaded RuleSet evaluation via pthreads

### Advanced Capabilities

- **Rule templates & inheritance** — `TEMPLATE` / `EXTENDS` keywords for reusable rule definitions
- **Custom functions** — Register C callbacks as expression functions (e.g., `strlen(x)`)
- **Expression compilation cache** — Cache parsed and verified rules by DSL hash
- **Performance monitoring** — Per-VM execution statistics (counts, timing)
- **Debug trace mode** — Step-by-step expression evaluation output

### AI Integration

- **Mock mode (offline)** — Keyword-based NL-to-DSL translation and structural rule safety review
- **LLM API mode** — OpenAI-compatible API via cURL for real translation and review
- **Streaming support** — Callback-based SSE streaming for translation and review

### Code Generation & Visualization

- **C code generation** — Translate DSL rules to executable C code
- **Graphviz DOT output** — Visualize rules and rule sets as directed graphs

---

## Quick Start

### Requirements

| Tool     | Minimum Version |
|----------|-----------------|
| C99 compiler (GCC / Clang) | —               |
| CMake    | 3.14            |
| Flex     | 2.6             |
| Bison    | 3.8             |

### Build & Run

```bash
git clone <repo-url>
cd cdsl
cmake -B build && cmake --build build -j$(nproc)

# Run demo (6 scenarios)
./build/cdsl_demo

# Run tests
ctest --test-dir build --output-on-failure

# Generate Doxygen docs
cmake --build build --target doc
# → build/docs/html/index.html
# Or view online: https://quintin-lee.github.io/cdsl/
```

### Demo Scenarios

| #  | Scenario                  | Description                                          |
|----|---------------------------|------------------------------------------------------|
| 1  | Supplier Qualification    | AI-generated DSL with blacklist + capital + scoring  |
| 2  | Document Format Audit     | Format (critical) + signature + size scoring         |
| 3  | Content Safety Audit      | Sensitive words + PII + spam scoring                 |
| 4  | JSON Context              | Variable bindings loaded from JSON string            |
| 5  | Simple Rules              | Independent pass/fail checks                         |
| 6  | RuleSet Batch             | Priority-ordered multi-rule execution                |

---

## Integration

### Method 1: add_subdirectory

```cmake
add_subdirectory(path/to/cdsl)
target_link_libraries(your_app PRIVATE cdsl)
```

### Method 2: Installed find_package

```bash
cmake --install build --prefix /usr/local
```

```cmake
find_package(cdsl REQUIRED)
target_link_libraries(your_app PRIVATE cdsl::cdsl_static)
```

### Method 3: pkg-config

```bash
pkg-config --cflags --libs cdsl
```

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│                  AI Bridge Layer                     │
│   NL → DSL translation + Safety Review              │
│   (mock / OpenAI-compatible API / stream)            │
└────────────────────┬────────────────────────────────┘
                     │ DSL String
                     ▼
┌─────────────────────────────────────────────────────┐
│  1. Syntax Layer (Flex/Bison → AST)                 │
│     parser/lexer.l → parser/parser.y                │
│     Output: cdsl_rule_t (AST)                       │
└────────────────────┬────────────────────────────────┘
                     │ Raw AST
                     ▼
┌─────────────────────────────────────────────────────┐
│  2. Abstract Layer (Schema Verification)            │
│     Type checking, variable resolution,             │
│     action signature validation                     │
└────────────────────┬────────────────────────────────┘
                     │ Verified AST
                     ▼
┌─────────────────────────────────────────────────────┐
│  3. Execution Layer (VM + Context)                  │
│     AST interpreter, scoring, report generation     │
│     RuleSet / parallel / debug / stats / codegen    │
└─────────────────────────────────────────────────────┘
```

See [Architecture Document](docs/architecture.md) for a detailed breakdown.

---

## DSL Syntax

### Simple Rule (WHEN/THEN)

```
RULE check_blacklist {
    META { description = "Blacklist check" }
    WHEN supplier.is_blacklisted == true
    THEN reject_supplier("blacklisted")
}
```

- WHEN **true** → trigger THEN → status **FAILED**
- WHEN **false** → no action → status **PASSED**

### Scoring Rule (METRIC/CASE/DEFAULT)

```
RULE supplier_audit {
    META { description = "Supplier audit" pass_threshold = "80" partial_threshold = "60" }
    METRIC credit_check {
        META { weight = "30" is_critical = "true" }
        CASE supplier.is_blacklisted == false THEN score(30)
        DEFAULT fail_metric(0, "blacklisted")
    }
    METRIC capital_check {
        META { weight = "40" }
        CASE supplier.capital >= 5000000 THEN score(40)
        CASE supplier.capital >= 1000000 THEN score(20)
        DEFAULT score(0)
    }
}
```

### Template Inheritance

```
TEMPLATE base_metric {
    META { description = "Base template" }
    METRIC base { META { weight = "100" } DEFAULT score(0) }
}

RULE my_rule EXTENDS base_metric {
    METRIC custom { META { weight = "50" } CASE x > 10 THEN score(50) DEFAULT score(0) }
}
```

See [DSL Syntax Reference](docs/dsl-syntax.md) for full grammar details.

---

## Project Structure

```
cdsl/
├── include/          # Public headers (8 files)
├── src/              # Core implementation
│   ├── ast.c         #         AST construction & free
│   ├── abstract.c    #    Schema verification
│   ├── execution.c   #    VM, context, RuleSet, stats, codegen, visualization
│   ├── ai_bridge.c   #    NL ↔ DSL translation & review
│   ├── cdsl_json.c   #    Zero-dependency JSON parser
│   ├── cdsl_error.c  #    Structured error reporting
│   ├── cdsl_arena.c  #    Arena memory allocator
│   └── cdsl_hashmap.c#    Hash table
├── parser/           # Flex/Bison grammar
│   ├── lexer.l
│   └── parser.y
├── demo/main.c       # 6 demo scenarios
├── tests/            # Unit tests (41+ tests)
├── docs/             # Documentation
│   ├── architecture.md
│   ├── api-reference.md
│   ├── dsl-syntax.md
│   ├── modules.md
│   └── user-guide.md
└── cmake/            # Build configuration
```

---

## API Overview

```c
// Schema
cdsl_schema_t* schema = cdsl_schema_create();
cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);

// Parse
cdsl_rule_t* rule = cdsl_parse_string("RULE ...");
cdsl_verify_rule(rule, schema, err_buf, sizeof(err_buf));

// Execute
cdsl_vm_t* vm = cdsl_vm_create(schema);
cdsl_vm_register_action(vm, "block", my_callback);
cdsl_context_t* ctx = cdsl_context_create(schema);
cdsl_context_set_int(ctx, "user.age", 25);
cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);

// Batch
cdsl_ruleset_t* set = cdsl_ruleset_create();
cdsl_ruleset_add(set, rule, 1);
cdsl_ruleset_report_t* batch = cdsl_vm_execute_ruleset(vm, set, ctx);

// AI
cdsl_ai_config_t cfg = cdsl_ai_config_default();
char* dsl = cdsl_ai_translate("supplier blacklist check", schema, &cfg);
cdsl_ai_review_t* review = cdsl_ai_review(dsl, schema, &cfg);
```

See [API Reference](docs/api-reference.md) for the complete function documentation.

---

## Documentation

| Document               | Audience           | Description                            |
|------------------------|--------------------|----------------------------------------|
| [API Reference](docs/api-reference.md)    | Developers         | Complete API function and type documentation |
| [Architecture](docs/architecture.md)      | Architects         | System design, data flow, threading    |
| [DSL Syntax](docs/dsl-syntax.md)          | Rule authors       | Complete language syntax and examples  |
| [Modules](docs/modules.md)                | Contributors       | Internal module design and data structures |
| [User Guide](docs/user-guide.md)          | Users              | Step-by-step usage with code examples  |
| [Doxygen](https://quintin-lee.github.io/cdsl/) | Developers     | Generated API documentation (GitHub Pages) |

---

## Thread Safety

| Operation                          | Thread Safe | Notes                                    |
|------------------------------------|-------------|------------------------------------------|
| `cdsl_parse_string()`              | ✅          | Uses reentrant Flex/Bison scanner        |
| `cdsl_vm_execute()`                | ✅          | Each thread needs its own VM instance    |
| `cdsl_context_*()`                 | ✅          | Each thread needs its own Context        |
| Schema (read-only)                 | ✅          | Can be shared across threads             |
| Rule (read-only)                   | ✅          | Can be shared after parsing              |
| `cdsl_compile_cache_t`             | ✅          | Internal RWLock protection               |

---

## License

MIT. See [LICENSE](LICENSE) for details.
