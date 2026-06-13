# C-DSL Rule Engine

[![CI](https://github.com/quintin-lee/cdsl/actions/workflows/ci.yml/badge.svg)](https://github.com/quintin-lee/cdsl/actions/workflows/ci.yml)
[![Lint](https://github.com/quintin-lee/cdsl/actions/workflows/ci.yml/badge.svg?job=clang-tidy)](https://github.com/quintin-lee/cdsl/actions/workflows/ci.yml)
[![Static Analysis](https://github.com/quintin-lee/cdsl/actions/workflows/ci.yml/badge.svg?job=static-analysis)](https://github.com/quintin-lee/cdsl/actions/workflows/ci.yml)
[![Coverage](https://img.shields.io/badge/coverage-~61%25-yellow)](https://github.com/quintin-lee/cdsl/actions/workflows/ci.yml)
[![Fuzz](https://github.com/quintin-lee/cdsl/actions/workflows/ci.yml/badge.svg?job=fuzz)](https://github.com/quintin-lee/cdsl/actions/workflows/ci.yml)
[![Docs](https://img.shields.io/badge/docs-github--pages-blue)](https://quintin-lee.github.io/cdsl/)
[![License: MIT](https://img.shields.io/badge/license-MIT-green)](LICENSE)
[![C Standard](https://img.shields.io/badge/C-23-blue)](https://en.wikipedia.org/wiki/C23)
[![Platforms](https://img.shields.io/badge/platforms-Linux%20|%20macOS%20|%20Windows-lightgrey)](https://github.com/quintin-lee/cdsl/actions)

**C-DSL** is an AI-powered domain-specific language rule engine for business rule validation. It translates natural language rules into executable DSL, evaluates them with multi-metric scoring, and produces tri-state audit reports (PASSED / PARTIALLY PASSED / FAILED).

---

## Table of Contents

- [Features](#features)
- [Quick Start](#quick-start)
- [Docker](#docker)
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
- **Bytecode VM** — AST→bytecode compiler with 24-instruction ISA, constant folding, short-circuit evaluation
- **Sandboxing** — Per-execution timeout, memory limit, instruction quota, read-only variables

### AI Integration

- **Mock mode (offline)** — Keyword-based NL-to-DSL translation and structural rule safety review
- **LLM API mode** — OpenAI-compatible API via cURL for real translation and review
- **Streaming support** — Callback-based SSE streaming for translation and review

### Code Generation & Visualization

- **C code generation** — Translate DSL rules to executable C code with codegen API
- **Graphviz DOT output** — Visualize rules and rule sets as directed graphs

### Build & CI

- **Multi-platform** — Linux (GCC), macOS (Apple Clang), Windows (MSVC), Docker
- **Static analysis** — clang-tidy (30+ check groups), cppcheck, scan-build; all treated as errors
- **Fuzz testing** — libFuzzer-based fuzz target with ASan
- **Performance benchmarks** — 7-scenario benchmark suite
- **ccache-aware** — Auto-detects ccache for faster rebuilds

---

## Quick Start

### Requirements

| Tool     | Minimum Version |
|----------|-----------------|
| C23 compiler (GCC / Clang / MSVC) | —               |
| CMake    | 3.19            |
| Flex     | 2.6             |
| Bison    | 3.8             |

### Build & Run

```bash
git clone <repo-url>
cd cdsl
cmake -B build && cmake --build build -j$(nproc)

# (Optional) Install pre-commit hook for auto-formatting
cmake --build build --target install-git-hooks

# Run demo (6 scenarios)
./build/cdsl_demo

# Run tests (22+)
ctest --test-dir build --output-on-failure

# Run clang-tidy on source files
cmake --build build --target check-tidy

# Generate Doxygen docs
cmake --build build --target doc
# → build/docs/html/index.html
# Or view online: https://quintin-lee.github.io/cdsl/

# Uninstall (if installed)
cmake --build build --target uninstall
```

### CMake Options

| Option                    | Default | Description                                |
|---------------------------|---------|--------------------------------------------|
| `CDSL_BUILD_FUZZ`         | OFF     | Build libFuzzer fuzz target (requires Clang) |
| `CDSL_BUILD_LSP`          | OFF     | Build cdsl-lsp language server             |
| `CDSL_BUILD_BENCHMARKS`   | OFF     | Build performance benchmarks               |
| `CDSL_UNITY_BUILD`        | OFF     | Enable jumbo / unity compilation           |
| `CDSL_ENABLE_COVERAGE`    | OFF     | Enable gcov code coverage (Debug only)     |
| `CDSL_GENERATE_DOCS`      | OFF     | Auto-generate Doxygen on every build       |
| `CDSL_FORMAT_ON_BUILD`    | OFF     | Run clang-format before every build        |

Example:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCDSL_ENABLE_COVERAGE=ON
cmake --build build -j$(nproc)
ctest --test-dir build --output-on-failure
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

## Docker

```bash
# Development image (build tools + runtime)
docker build --target dev -t cdsl-dev .
docker run -it --rm -v $(pwd):/cdsl cdsl-dev

# Minimal runtime image (pre-built binaries only)
docker build --target release -t cdsl-runtime .
docker run --rm cdsl-runtime /usr/local/bin/cdsl_demo
```

---

## Integration

### Method 1: Installed (find_package)

Build and install:
```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
cmake --install build --prefix /usr/local           # all components
cmake --install build --component libraries         # libraries only
cmake --install build --component demos             # demo executables
```

```cmake
find_package(cdsl REQUIRED)
target_link_libraries(your_app PRIVATE cdsl::cdsl_static)  # or cdsl::cdsl_shared
```

### Method 2: add_subdirectory

```cmake
add_subdirectory(path/to/cdsl)
target_link_libraries(your_app PRIVATE cdsl)
```

### Method 3: pkg-config

```bash
pkg-config --cflags --libs cdsl
```

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│                     AI Layer                         │
│   NL → DSL translation + Safety Review              │
│   (mock / OpenAI-compatible API / stream)            │
└────────────────────┬────────────────────────────────┘
                     │ DSL String
                     ▼
┌─────────────────────────────────────────────────────┐
│  1. Syntax Layer (Flex/Bison → AST)                 │
│     parser/ → src/ast/                              │
│     Output: cdsl_rule_t (AST)                       │
└────────────────────┬────────────────────────────────┘
                     │ Raw AST
                     ▼
┌─────────────────────────────────────────────────────┐
│  2. Schema Layer (Schema Verification)              │
│     Type checking, variable resolution,             │
│     action signature validation                     │
└────────────────────┬────────────────────────────────┘
                     │ Verified AST
                     ▼
┌─────────────────────────────────────────────────────┐
│  3. Execution Layer (VM + Context)                  │
│     AST interpreter / Bytecode VM                  │
│     Scoring, report generation, debug trace         │
│     RuleSet / parallel / stats / codegen / visual   │
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
├── include/cdsl/     # Public headers
│   ├── cdsl.h        # Umbrella header (includes all below)
│   ├── ast.h         # AST construction
│   ├── schema.h      # Schema verification
│   ├── context.h     # Variable bindings
│   ├── vm.h          # VM lifecycle
│   ├── report.h      # Report creation
│   ├── cache.h       # Compilation cache
│   ├── ruleset.h     # Batch execution
│   ├── codegen.h     # DSL → C code generation
│   ├── visual.h      # Graphviz DOT output
│   ├── execution.h   # Backward-compat shim
│   ├── ai.h          # AI integration
│   └── util/         # Infrastructure
│       ├── arena.h
│       ├── error.h
│       ├── hashmap.h
│       └── json.h
├── src/              # Implementation
│   ├── ast/          # AST module
│   │   ├── ast.c
│   │   ├── parse.c
│   │   └── template.c
│   ├── schema/
│   │   └── schema.c  # Schema verification
│   ├── vm/           # Execution module (7 sub-modules)
│   │   ├── context.c
│   │   ├── vm.c
│   │   ├── eval.c
│   │   ├── report.c
│   │   ├── cache.c
│   │   ├── ruleset.c
│   │   ├── codegen.c
│   │   ├── bytecode.c
│   │   ├── builtins.c
│   │   └── visual.c
│   ├── ai/
│   │   └── bridge.c  # AI translation & review
│   └── util/         # Infrastructure
│       ├── arena.c
│       ├── error.c
│       ├── hashmap.c
│       └── json.c
├── parser/           # Flex/Bison grammar
│   ├── lexer.l
│   └── parser.y
├── demo/
│   ├── main.c        # 6 demo scenarios
│   └── official_review.c
├── tests/            # Unit tests (22), fuzz, benchmarks
├── docs/             # Documentation
├── cmake/            # Build configuration
└── .github/          # CI workflows + PR template
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
