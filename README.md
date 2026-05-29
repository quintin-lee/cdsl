# C-DSL Rule Engine

An AI-powered DSL rule engine framework in C with three-layer architecture (Syntax → Abstract → Execution), natural language translation, multi-metric scoring, and tri-state audit results.

## Features

### Core Engine
- **Three-layer architecture**: Syntax (Flex/Bison) → Abstract (Schema verification) → Execution (VM)
- **Dual rule types**: Simple WHEN/THEN pass-fail rules and multi-metric METRIC/CASE/DEFAULT scoring rules
- **Tri-state results**: PASSED / PARTIALLY_PASSED / FAILED with weighted scoring and critical-rule veto
- **Zero external dependencies**: Custom JSON parser, arena allocator, hash map — no third-party libs

### Rule Management
- **RuleSet batch execution**: Priority-ordered rule groups with aggregate reports
- **Hot reload**: Load, remove, and reload rules at runtime from files or strings
- **Dependency & topology**: `depends_on` metadata with topological sort for ordered execution
- **Parallel execution**: Multi-threaded RuleSet evaluation via pthreads

### Advanced Features
- **Rule templates & inheritance**: `TEMPLATE`/`EXTENDS` keywords for reusable rule definitions
- **Custom functions**: Register C callbacks as expression functions (e.g., `strlen(x)`)
- **Expression compilation cache**: Cache parsed+verified rules by DSL hash for repeated use
- **Performance monitoring**: Per-VM execution statistics (counts, timing)
- **Debug trace mode**: Step-by-step expression evaluation output

### AI Integration
- **Mock mode** (offline): Keyword-based NL-to-DSL translation and rule safety review
- **LLM API mode**: OpenAI-compatible API via curl for real translation and review
- **Streaming support**: Callback-based SSE streaming for translation and review

### Code Generation & Visualization
- **C code generation**: Translate DSL rules to executable C code
- **Graphviz DOT output**: Visualize rules and rule sets as directed graphs

## Quick Start

### Requirements

- C99 compiler (GCC / Clang)
- CMake 3.14+
- Flex 2.6+
- Bison 3.8+

### Build & Run

```bash
mkdir build && cd build
cmake ..
make -j$(nproc)

# Run demo (6 scenarios)
./cdsl_demo

# Run tests
ctest

# Generate Doxygen docs
make doc
# → docs/html/index.html
```

### Integration

**add_subdirectory:**
```cmake
add_subdirectory(path/to/cdsl)
target_link_libraries(your_app PRIVATE cdsl)
```

**Installed (find_package):**
```bash
cmake --install . --prefix /usr/local
```
```cmake
find_package(cdsl REQUIRED)
target_link_libraries(your_app PRIVATE cdsl::cdsl_static)
```

**pkg-config:**
```bash
pkg-config --cflags --libs cdsl
```

## Architecture

```
┌─────────────────────────────────────────────┐
│             AI Bridge Layer                  │
│   NL → DSL translation + Safety Review      │
│   (mock / OpenAI-compatible API / stream)    │
└──────────────────┬──────────────────────────┘
                   │ DSL String
                   ▼
┌─────────────────────────────────────────────┐
│  1. Syntax Layer (Flex/Bison → AST)         │
│     parser/lexer.l → parser/parser.y        │
│     Output: cdsl_rule_t (AST)               │
└──────────────────┬──────────────────────────┘
                   │ Raw AST
                   ▼
┌─────────────────────────────────────────────┐
│  2. Abstract Layer (Schema Verification)    │
│     Type checking, variable resolution,     │
│     action signature validation             │
└──────────────────┬──────────────────────────┘
                   │ Verified AST
                   ▼
┌─────────────────────────────────────────────┐
│  3. Execution Layer (VM + Context)          │
│     AST interpreter, scoring, report gen    │
│     RuleSet / parallel / debug / stats      │
└─────────────────────────────────────────────┘
```

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

## Project Structure

```
cdsl/
├── include/          # Public headers (8 files)
├── src/              # Core implementation
│   ├── ast.c         # AST construction & free
│   ├── abstract.c    # Schema verification
│   ├── execution.c   # VM, context, RuleSet, stats, codegen, visualization
│   ├── ai_bridge.c   # NL ↔ DSL translation & review
│   ├── cdsl_json.c   # Zero-dependency JSON parser
│   ├── cdsl_error.c  # Structured error reporting
│   ├── cdsl_arena.c  # Arena memory allocator
│   └── cdsl_hashmap.c# Hash table
├── parser/           # Flex/Bison grammar
│   ├── lexer.l
│   └── parser.y
├── demo/main.c       # 6 demo scenarios
├── tests/            # Unit tests (41 tests)
├── docs/             # Documentation (5 Markdown + Doxygen)
└── cmake/            # Build configuration
```

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

## Examples

- **Supplier audit**: Blacklist (critical) + capital + experience scoring
- **Document audit**: Format (critical) + signature + size scoring
- **Content moderation**: Sensitive words + PII + spam score
- **JSON context**: Load variable bindings from JSON
- **Simple rules**: Independent pass/fail checks
- **RuleSet batch**: Priority-ordered multi-rule execution

## Thread Safety

| Operation | Safe |
|-----------|------|
| `cdsl_parse_string()` | ❌ (Flex global state) |
| `cdsl_vm_execute()` | ✅ (per-thread VM) |
| `cdsl_context_*()` | ✅ (per-thread Context) |
| Schema read-only | ✅ |
| Rule read-only | ✅ |

## License

MIT
