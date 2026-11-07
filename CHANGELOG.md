# Changelog

## [Unreleased] - 2026-11-07

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

## [1.1.0] - 2026-06-13

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
