# Changelog

## [2.0.0] - 2026-11-08

### Added
- **Native Machine Code JIT Engine (`include/cdsl/jit.h`, `src/vm/jit.c`)**:
  - Pure C machine code JIT engine compiling CDSL bytecode to native machine instructions.
  - Multi-architecture native support for x86-64 (System V AMD64 and Windows x64) and AArch64 (Linux and macOS ARM64).
  - W^X executable memory protection with page-aligned mmap / VirtualProtect and automatic instruction cache synchronization.
  - Safe automatic fallback to threaded bytecode interpreter for non-supported architectures and sandboxed environments.
  - VM integration via `cdsl_vm_set_jit_enabled()` and fast-path execution.
- **Deterministic SHA-256 Version Fingerprinting (`include/cdsl/util/sha256.h`, `src/util/sha256.c`)**:
  - Zero-dependency standard C SHA-256 implementation.
  - Deterministic AST and ruleset fingerprinting (`cdsl_rule_hash`, `cdsl_ruleset_hash`) for cluster-wide version drift detection and audit trails.
- **Distributed Configuration Drivers & Cluster Governance (`include/cdsl/config_source.h`, `src/manager/`)**:
  - Pluggable config source abstraction (`cdsl_config_source_t`) for cluster synchronization and dynamic watch events.
  - Production-ready drivers: Memory mock driver, Etcd v3 driver (`src/manager/etcd.c`), and Consul KV driver (`src/manager/consul.c`).
  - Seamless integration with enterprise rule manager (`cdsl_manager_attach_config_source`).
  - Automated canary verification: incoming rule changes undergo sandbox parsing and trial verification before atomic RCU cutover, with instant self-healing rollback on validation failure.
- **5-Engine Cross-Equivalence Verification (`tests/test_jit.c`, `tests/test_distributed.c`)**:
  - Verification suite confirming 100% bitwise and semantic parity across AST, Bytecode, Threaded, JIT, and Columnar execution engines.
  - Comprehensive distributed governance tests validating Etcd, Consul, Mock CRUD, and canary rollbacks.

## [1.6.0] - 2026-11-08

### Added
- **Idiomatic Go CGO Binding (`bindings/go/`)**:
  - Full Go package `cdsl` providing safe memory management with `runtime.SetFinalizer`.
  - Complete wrappers for `Engine`, `Context`, `Rule`, `DecisionTable`, `Scorecard`, and `Pipeline`.
  - Comprehensive Go unit test suite (`bindings/go/cdsl_test.go`).
- **Memory-Safe Rust FFI Crate (`bindings/rust/`)**:
  - Full Rust crate `cdsl` with RAII `Drop` safety and thread-safe abstractions (`Send + Sync`).
  - Safe wrappers for `Engine`, `Context`, `Rule`, `DecisionTable`, `Scorecard`, and `Pipeline`.
  - Integration test suite (`bindings/rust/tests/integration_test.rs`).
- **WebAssembly / WASI Build & NPM Package (`wasm/`)**:
  - Exportable C bridge `wasm/cdsl_wasm.c` for WASI and Emscripten with JSON I/O.
  - Proxy-Wasm Envoy filter and API gateway support for sub-50 microsecond edge evaluation.
  - `@cdsl/engine` NPM package (`wasm/npm/`) with TypeScript declarations and Node.js/browser test suite.
- **Standalone Cloud-Native HTTP Micro-Daemon (`server/`, `src/server/server.c`, `include/cdsl/server.h`)**:
  - Lightweight embedded HTTP server (`cdsl-server`) with non-blocking socket I/O.
  - RESTful evaluation endpoint `POST /v1/evaluate` for rules, decision tables, scorecards, and pipelines.
  - Dynamic rule deployment endpoint `POST /v1/rules/deploy`.
  - Prometheus metrics exporter `GET /metrics` (`cdsl_http_requests_total`, `cdsl_evaluations_total`).
  - Kubernetes liveness probe `GET /healthz` and readiness probe `GET /readyz`.
  - Unit and integration tests in `tests/test_server.c`.
- **Decision Table Engine (`include/cdsl/dtable.h`, `src/vm/dtable.c`)**:
  - DMN-style multi-dimensional condition matching with column definitions for typed inputs and outputs.
  - Implemented three core hit policies: `CDSL_HIT_FIRST` (first match terminates), `CDSL_HIT_RULE_ORDER` (all matches collected), and `CDSL_HIT_UNIQUE` (strict uniqueness constraint).
  - Native wildcard cell support (`_`) matching any runtime value.
  - Structured evaluation report (`cdsl_dtable_report_t`) recording matched row IDs and evaluated output vectors.
  - Standalone DSL parser (`cdsl_dtable_parse_string`) and schema static validation (`cdsl_schema_verify_dtable`).
- **Standard Scorecard Modeling Engine (`include/cdsl/scorecard.h`, `src/vm/scorecard.c`)**:
  - Credit and risk scorecard modeling with baseline score initialization and multi-characteristic attribute binning.
  - Range and inequality condition matching across feature dimensions with positive/negative score increments.
  - Context variable injection for seamless downstream pipeline consumption.
  - Comprehensive interpretability reports (`cdsl_scorecard_report_t`) detailing per-characteristic contribution deltas.
  - Standalone DSL parser (`cdsl_scorecard_parse_string`) and schema verification (`cdsl_schema_verify_scorecard`).
- **DAG Rule Pipeline Orchestration Engine (`include/cdsl/pipeline.h`, `src/vm/pipeline.c`)**:
  - Multi-stage execution pipeline supporting rules, decision tables, and scorecards as first-class stage items.
  - Dependency checking and topological scheduling between named stages.
  - Context data flow propagation: outputs from earlier stages immediately available to downstream stages.
  - Configurable failure policies (`CDSL_ON_FAIL_ABORT` vs `CDSL_ON_FAIL_CONTINUE`) with stage-level timing and execution reports (`cdsl_pipeline_report_t`).
  - Standalone DSL parser (`cdsl_pipeline_parse_string`).
- **Test Coverage**:
  - Added test suites `tests/test_dtable.c`, `tests/test_scorecard.c`, and `tests/test_pipeline.c` expanding test suite to 42 test cases.

## [1.4.0] - 2026-11-08

### Added
- **Enterprise Rule Manager & RCU Hot Reload (`include/cdsl/manager.h`, `src/manager/manager.c`)**:
  - Implemented lock-free Copy-On-Write (RCU) hot reload manager (`cdsl_manager_t`) for zero-downtime ruleset deployments.
  - In-flight execution reader safety through atomic ref-counting and epoch snapshot handles (`cdsl_manager_acquire`, `cdsl_manager_release`).
  - Ring buffer retaining up to 8 historical version snapshots with instant nanosecond-level rollback (`cdsl_manager_rollback`).
  - Unified manager execution helper (`cdsl_manager_execute`) ensuring transparent snapshot acquisition and release.
- **Canary Pre-Flight Verification Gate (`include/cdsl/canary.h`, `src/manager/canary.c`)**:
  - Automated pre-flight validation pipeline checking DSL syntax, static schema types, and isolated dry-run execution against mock business contexts.
  - Protected deployment API (`cdsl_manager_deploy_canary`) automatically rejecting invalid or crashing rule updates before touching production traffic.
- **Prometheus Metrics Exposition (`include/cdsl/metrics.h`, `src/obs/metrics.c`)**:
  - Standard Prometheus text format (v0.0.4) metrics collector and exporter (`cdsl_metrics_export_prometheus`).
  - Real-time counters for rule executions by status (`passed`, `partially_passed`, `failed`, `error`), custom actions, and gauges for system state.
  - High-precision latency histograms covering latency distributions from 0.1ms to 5.0s.
- **OpenTelemetry Decision Tracing (`include/cdsl/trace.h`, `src/obs/trace.c`)**:
  - Standard W3C TraceContext header support (`traceparent: 00-{trace_id}-{span_id}-01`) for end-to-end distributed transaction tracking.
  - Fine-grained audit decision spans recording evaluation trees, metric scores, rule triggers, and custom attributes.
  - OpenTelemetry OTLP JSON export format (`cdsl_trace_export_json`) enabling seamless integration with Jaeger, Zipkin, Datadog, and Grafana Tempo.
- **Test Coverage**:
  - Added test suites `tests/test_manager.c`, `tests/test_canary.c`, `tests/test_metrics.c`, and `tests/test_opentelemetry.c`.
  - 100% clean verification across all 39 test suites under AddressSanitizer, LeakSanitizer, UndefinedBehaviorSanitizer, and ThreadSanitizer.

## [1.3.0] - 2026-11-08

### Added
- **Direct-Threaded Bytecode Dispatch Engine (`src/vm/threaded.c`)**:
  - Implemented GNU C computed goto dispatch (`&&label` jump tables) eliminating central switch statement branch misprediction overhead.
  - Provided clean, portable fallback for standard C compilers and MSVC.
  - Exposed `cdsl_bytecode_execute_threaded` in public bytecode API (`include/cdsl/bytecode.h`).
- **SIMD-Vectorized Columnar Batch Engine (`include/cdsl/columnar.h`, `src/vm/columnar.c`)**:
  - Added columnar execution context (`cdsl_columnar_ctx_t`) supporting contiguous storage for `INT`, `LONG`, `FLOAT`, `BOOL`, and `STRING` columns.
  - Auto-vectorized batch rule condition evaluation (`cdsl_columnar_eval_rule`) achieving over **105 Million rows/sec** throughput.
  - Support for relational operators (`==`, `!=`, `<`, `<=`, `>`, `>=`), closed interval ranges (`in [min..max]`), and logical operators (`AND`, `OR`, `NOT`).
  - Added 10,000-row differential equivalence test against standard row VM in `tests/test_columnar.c`.
- **Common Subexpression Elimination (CSE) & Shared Predicate Cache (`include/cdsl/cse.h`, `src/vm/cse.c`)**:
  - Implemented 64-bit structural AST hashing (`cdsl_expr_hash`) using FNV-1a.
  - Implemented deep structural AST equality (`cdsl_expr_equal`) covering all 16 AST node types.
  - Added transaction-level predicate evaluation cache (`cdsl_pred_cache_t`) for sharing condition and subexpression results across rules in a ruleset.
  - Integrated predicate caching automatically into ruleset batch execution (`cdsl_vm_execute_ruleset`) and exposed VM configuration API (`cdsl_vm_enable_pred_cache`, `cdsl_vm_set_pred_cache`).
- **Benchmarks & Tooling**:
  - Extended `tests/bench_execution.c` to benchmark direct-threaded execution and vectorized columnar batch throughput.
  - Added new test suites `tests/test_threaded.c`, `tests/test_columnar.c`, and `tests/test_cse.c`.

## [1.2.0] - 2026-11-07

### Added
- **Language Expressiveness & Collections**:
  - **Map Literals & Types (`CDSL_TYPE_MAP`)**: Added native map literal syntax `{ key: expr, ... }`, runtime hash-bucket based map data structure (`cdsl_map_t`), deep-copying, and memory-safe lifetime management.
  - **Property & Subscript Access**: Supported unified indexing `expr[index]` and dot access `expr.prop` for maps and arrays across AST tree-walk and bytecode VM execution paths.
  - **Interval & Membership Operators (`in`)**: Added `expr in [min..max]` closed interval operator and `expr in container` membership testing for arrays, map keys, and substring containment.
  - **Higher-Order Stream Predicates**: Implemented lambda expressions `IDENTIFIER -> expr` and higher-order built-in predicates `any(list, lambda)`, `all(list, lambda)`, and `filter(list, lambda)` with short-circuit evaluation.
- **Static Type Inference & Analysis**:
  - Enhanced schema validator (`resolve_expr_type`) to perform compile-time interval contradiction detection (e.g., rejecting dead-code ranges `[20..10]`).
  - Added compile-time validation for collection membership types and subscript target constraints.
  - Permissive dynamic type checking for nested Map properties, ensuring full compatibility between static schemas and dynamic JSON/Map data models.
- **Bytecode VM Extensions**:
  - Added new instructions `BC_BUILD_MAP`, `BC_GET_PROP`, `BC_GET_INDEX`, `BC_IN`, and `BC_IN_RANGE`.
  - Added bytecode disassembly and graph visualization support for the new AST and bytecode instructions.
- **Test Coverage**:
  - Added unit test suites `tests/test_map.c`, `tests/test_collection_ops.c`, and `tests/test_type_inference.c`.
  - Verified 100% clean memory safety under AddressSanitizer, LeakSanitizer, and UndefinedBehaviorSanitizer.
  - Updated Python bindings and test suite for `Type.MAP` and `Type.VOID`.

## [1.1.0] - 2026-11-07

### Added
- **Exact Floating-Point Equality & Approximate Matching**:
  - Restored IEEE 754 strict equality comparison (`==` and `!=`) for floating-point values in both AST tree-walk and bytecode VM execution engines.
  - Added built-in function `approx_eq(a, b, [epsilon])` with default tolerance $10^{-6}$ for business rules requiring fuzzy float comparisons.
  - Added cross-engine differential tests in `tests/test_differential.c` and Python test coverage.
- **Thorough OOM & Exception Safety Closure**:
  - Replaced all unchecked raw `strdup`, `malloc`, and `calloc` calls across all non-demo C modules with safe wrappers (`cdsl_strdup`, `cdsl_vm_strdup`, `cdsl_vm_calloc`).
  - Implemented transactional unwinding in AST node creation (`src/ast/ast.c`): child nodes are recursively freed on intermediate allocation failures.
  - Added cascading deallocation and NULL guards in template copy functions (`src/ast/template.c`) and safe error list ownership transfer (`src/util/error.c`).
  - Hardened string buffer dynamic re-allocation (`cdsl/util/strbuf.h`) and arena duplication against OOM.
  - Added 5 comprehensive OOM fault-injection tests in `tests/test_memory.c` verifying zero memory leaks under AddressSanitizer/LeakSanitizer.
- **Unified evaluation semantics (`src/vm/value_ops.{c,h}`)**:
  - Unified binary and unary operator evaluation between AST tree-walk (`eval.c`), bytecode execution (`bytecode.c`), and compile-time constant folding (`fold_const_expr`).
  - Checked arithmetic for 32-bit `INT` and 64-bit `LONG` with overflow detection.
  - Exact 64-bit comparison for `LONG` avoiding IEEE 754 precision loss on values exceeding $2^{53}$.
  - Strict runtime error policy: division by zero, arithmetic overflow, and invalid string relational comparisons (`<`, `>`, `<=`, `>=`) set `vm->last_error` and return `CDSL_STATUS_ERROR`.
  - Bytecode stack safety hardening: compilation fails if `max_stack > CDSL_BYTECODE_MAX_STACK`, and runtime stack push/pop/peek operations raise runtime errors on overflow/underflow.
  - Comprehensive differential execution test suite (`tests/test_differential.c`) and differential fuzzing in `tests/fuzz_parse.c`.
- **Sandbox & Security hardening**:
  - Cumulative per-execution memory quota enforcement via `cdsl_vm_strdup` / `cdsl_vm_calloc` and `cdsl_vm_set_memory_limit`.
  - Execution lifecycle reset via `cdsl_vm_begin_execution` ensuring instruction and memory counters do not leak across runs on the same VM.
  - Safe shell escaping (`cdsl_ai_escape_shell_sq_internal`, `'"'"'`) preventing command injection in the AI bridge; disabled `popen` curl fallback on Windows.
- **Build & CI improvements**:
  - Generated `<cdsl/version.h>` from `cmake/version.h.in` with `CDSL_VERSION_MAJOR`, `MINOR`, `PATCH`, and `STRING`.
  - Exported `$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>` on `cdsl_static` and `cdsl_shared` targets.
  - Added standalone public header compilation check target (`check-headers`) under strict `-std=c2x`.
  - Added Clang ThreadSanitizer (TSan) job and removed experimental `continue-on-error` on Windows in GitHub Actions CI.

### Fixed
- Fixed memory leak in parallel ruleset execution by introducing `cdsl_vm_init_worker()` and `cdsl_vm_release_worker()` to properly manage worker VM lifecycles and error state.
- Fixed $O(N^2)$ rule lookup in parallel execution by pre-indexing rules into an array ($O(1)$ access).
- Fixed thread join crash risk by tracking successfully created thread IDs before joining.
- Fixed `codegen.c` float literal precision loss using `%.17g` (guaranteeing decimal dot or exponent) and 64-bit integer literal formatting using `%lldLL` with safe `INT64_MIN` handling.
- Fixed glibc strict C23 header compatibility for POSIX rwlock feature guards in `<cdsl/util/threads.h>` and `<cdsl/util/portability.h>`.
- Resolved Bison warnings in `parser/parser.y` (`%precedence NOT UMINUS`, `%empty`).
- Fixed invalid `ctest` command invocation in CMake `cdsl_coverage` target.

### Changed
- **Documentation sync**: Updated all docs (`README.md`, `docs/architecture.md`, `docs/modules.md`, `docs/api-reference.md`, `docs/user-guide.md`, `docs/dsl-syntax.md`) to match the current codebase:
  - Bytecode ISA corrected from "24-instruction" to **31-instruction** (full `bc_op_t` enum in `<cdsl/bytecode.h>`)
  - Execution layer file name corrected `src/vm/visual.c` → `src/vm/visualize.c` throughout; non-existent `report.c` removed from structure listings
  - Execution layer re-counted as **9 sub-modules** (added `bytecode.c`, `builtins.c`, `vm.c` to the source-layout tables)
  - Documented the **dual execution path** (tree-walk in `eval.c` + bytecode in `bytecode.c`, dispatched by `cdsl_vm_execute_compiled()`)
  - `cdsl_parse_string()` documented as **thread-safe** (reentrant Flex/Bison scanner + thread-local arena), fixing the old "Not thread-safe (Flex uses global state)" claim
  - `cdsl_compiled_rule_t` struct now shows the `cdsl_bytecode_t bc` member; `cdsl_compile_cache_t` lock type corrected to `cdsl_rwlock_t`
  - `cdsl_stats_t` counters documented as `cdsl_atomic_long_t` (C23 `_Atomic`)
  - DSL syntax reference: added **array literal** section (§1.5), clarified string-literal notes; user guide built-in-function table now lists `count`/`sum` and extended `typeof` return-type set to include `LONG`/`ARRAY`
  - User-guide section numbering fixed (was jumping 12 → 15; now continuous 1–21) and table of contents aligned
  - `CDSL_GENERATE_DOCS` default corrected ON → **OFF** in `docs/architecture.md`

## [1.0.1] - 2026-06-13

### Added
- **Compiler hardening**: 10+ GCC/Clang warning flags promoted to errors (`-Wshadow`, `-Wformat=2`, `-Wnull-dereference`, `-Wdouble-promotion`, `-Wundef`, etc.)
- **ccache support**: Auto-detected compiler launcher for faster rebuilds
- **Docker CI**: `docker build --target dev` / `release` with smoke tests
- **CI caching**: `actions/cache` for ccache + apt packages
- **CMake uninstall target**: `cmake --build build --target uninstall`
- **PR template**: `.github/PULL_REQUEST_TEMPLATE.md` with build/test/format checklist
- **`.git-blame-ignore-revs`**: Skip formatting-only commits in `git blame`
- **clang-tidy on test files**: CI runs clang-tidy on `tests/` (informational)
- **MSVC guard for Clang flags**: `-Wno-*` flags guarded with `if(NOT MSVC)` / `GNU|Clang`

### Changed
- **Static analysis**: cppcheck `--error-exitcode=0` → `1` (fail on real warnings); suppress `unusedFunction`/`unmatchedSuppression`
- **Compiler flags**: `-Wcovered-switch-default` → `-Wno-covered-switch-default` for Apple Clang compat
- **Test targets**: Suppressed `-Wno-null-dereference`, `-Wno-sign-compare`, `-Wno-unused-result` to fix GCC Release false positives
- **`ast_alloc`**: Now `abort()` on OOM instead of returning NULL (eliminates `-Wnull-dereference` cascade)
- **Generated files**: `set_source_files_properties` for lexer.c/parser.tab.c with `-Wno-null-dereference`
- **README**: Updated CMake options table, added Docker/ccache/platform badges, updated project structure

### Fixed
- Docker: `useradd -u 1000` conflict with Ubuntu 24.04 base image → removed explicit UID
- Docker: GCC Release `-Wnull-dereference` on `ast.c` + generated files + test files
- Docker: GCC `-Wunused-result` on `fread()` in `test_codegen.c`
- macOS: `-Wcovered-switch-default` + `-Werror` breaks on Apple Clang
- Windows: MSVC `D8021` invalid flag `-Wno-null-dereference`

## [1.0.0] - 2026-05-31

### Added
- **CDSL_TYPE_LONG**: 64-bit signed integer type with `L` suffix (`9999999999L`)
- **11 built-in functions**: `uppercase`, `lowercase`, `trim`, `startswith`, `endswith`, `abs`, `min`, `max`, `round`, `typeof`, `date_add`
- **Bytecode VM**: AST→bytecode compiler + stack VM with 31-instruction ISA, constant folding, short-circuit evaluation
- **Static analysis**: `cdsl_analyze_rule()` with dead CASE detection, tautology/contradiction detection, shadowed CASE detection
- **Sandboxing**: per-execution timeout (`cdsl_vm_set_timeout`), memory limit (`cdsl_vm_set_memory_limit`), instruction limit, read-only variables
- **Engineering tools**: execution tracer API (`cdsl_vm_set_trace_callback`), LSP server (`tools/cdsl-lsp`, optional build via `-DCDSL_BUILD_LSP=ON`)
- **Structured error reporting**: parse errors now return `cdsl_error_list_t*` with line numbers and verbose messages (`%define parse.error verbose`)
- **Build system**: `pkg-config` support (`cdsl.pc`), CMake config package (`cdslConfig.cmake`), shared library `SOVERSION=1`

### Changed
- **API**: `cdsl_parse_string()` now takes optional `cdsl_error_list_t**` parameter
- **JSON enum**: Anonymous `enum { JSON_* }` → named `cdsl_json_type_t` with `CDSL_JSON_*` prefix
- **Struct tags**: 9 anonymous `typedef struct { }` → named struct tags for forward-declaration support
- **Schema**: Hashmap-based `find_var`/`find_action` (was O(n) linked-list), `cdsl_schema_register_var_rw()` for read-only variables
- **Build**: `add_definitions` → `target_compile_definitions`, `libm` linked PUBLIC

### Fixed
- Security: `strcat` → `snprintf` in AI bridge (buffer overflow), `calloc` NULL checks, `fread` return check
- Race condition: `init_registries()` uses `pthread_once`
- Memory leak: provider re-register frees old entry, array allocations freed in built-in functions
- Documentation: `cdsl_rule_t`, `cdsl_compile_cache_t` struct comments, missing API docs added
- CI: clang-format violations across 9 files, ASan+LSan clean

### Removed
- `cdsl_hashmap_contains` → renamed to `cdsl_hashmap_has`
- Redundant `THREAD_LOCAL` declarations from source files (consolidated in `ast.h`)
