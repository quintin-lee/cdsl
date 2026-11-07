# Changelog

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
