# Changelog

## [1.0.0] - 2026-05-31

### Added
- **CDSL_TYPE_LONG**: 64-bit signed integer type with `L` suffix (`9999999999L`)
- **11 built-in functions**: `uppercase`, `lowercase`, `trim`, `startswith`, `endswith`, `abs`, `min`, `max`, `round`, `typeof`, `date_add`
- **Bytecode VM**: AST→bytecode compiler + stack VM with 24-instruction ISA, constant folding, short-circuit evaluation
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
