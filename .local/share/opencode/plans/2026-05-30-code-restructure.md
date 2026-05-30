# Code Restructure: C-DSL Rule Engine

## Goal
Restructure the C-DSL Rule Engine codebase from a flat monolithic layout into a modular subdirectory architecture.

## Status: COMPLETED

## What Was Done

### Phase 1 - Infrastructure modules (`util/`)
| Module | Old Location | New Location |
|--------|-------------|--------------|
| arena  | `include/cdsl_arena.h` + `src/cdsl_arena.c` | `include/cdsl/util/arena.h` + `src/util/arena.c` |
| error  | `include/cdsl_error.h` + `src/cdsl_error.c` | `include/cdsl/util/error.h` + `src/util/error.c` |
| hashmap| `include/cdsl_hashmap.h` + `src/cdsl_hashmap.c` | `include/cdsl/util/hashmap.h` + `src/util/hashmap.c` |
| json   | `include/cdsl_json.h` + `src/cdsl_json.c` | `include/cdsl/util/json.h` + `src/util/json.c` |

C23 enhancements applied: `[[nodiscard]]`, `bool` returns, `constexpr`, `static_assert`.

### Phase 2 - Schema module
| Module | Old Location | New Location |
|--------|-------------|--------------|
| schema | `include/abstract.h` + `src/abstract.c` | `include/cdsl/schema.h` + `src/schema/schema.c` |

API changes: `cdsl_verify_rule` returns `bool`, `cdsl_schema_register_action` takes `cdsl_type_t`.

### Phase 3 - AST split
| Module | Old Location | New Location |
|--------|-------------|--------------|
| ast    | `src/ast.c` (741 lines, monolithic) | `src/ast/ast.c` (constructors + free), `src/ast/parse.c` (parser bridge), `src/ast/template.c` (template registry + deep-copy) |

### Phase 4 - VM module
| Module | Old Location | New Location |
|--------|-------------|--------------|
| context| `src/vm_context.c` | `src/vm/context.c` |
| eval   | `src/vm_eval.c` | `src/vm/eval.c` |
| ruleset| `src/vm_ruleset.c` | `src/vm/ruleset.c` |
| cache  | `src/vm_cache.c` | `src/vm/cache.c` |
| codegen| `src/vm_codegen.c` | `src/vm/codegen.c` |
| visualize| `src/vm_visualize.c` | `src/vm/visualize.c` |
| builtins| `src/vm_builtins.c` | `src/vm/builtins.c` |
| internal| `src/execution_internal.h` | `src/vm/internal.h` |

### Phase 5 - AI bridge
| Module | Old Location | New Location |
|--------|-------------|--------------|
| ai     | `include/ai_bridge.h` + `src/ai_bridge.c` | `include/cdsl/ai.h` + `src/ai/bridge.c` |

### Phase 6 - Remaining headers
| Module | Old Location | New Location |
|--------|-------------|--------------|
| ast    | `include/ast.h` | `include/cdsl/ast.h` |
| execution| `include/execution.h` | `include/cdsl/execution.h` |

Umbrella header `include/cdsl/cdsl.h` updated to reference only existing modules.

### Phase 7 - CMake
- C standard bumped from C99 to C23
- Source glob changed from `src/*.c` to `GLOB_RECURSE src/*.c`
- Install directory path fixed for new layout
- Format glob extended for subdirectories

## Verification
- Full build: 0 errors, 0 warnings
- All 7 test suites: 100% passed
- Demo application: runs correctly

## Outcome
- `include/` is now clean (only `cdsl/` directory remains)
- `include/cdsl/` contains all 7 public headers
- `src/` subdirectories: `util/` (4), `ast/` (3), `schema/` (1), `vm/` (8 including internal.h), `ai/` (1)
- No stale old-style includes remain
- Backward compatibility preserved where possible; `bool` returns and `[[nodiscard]]` are additive
