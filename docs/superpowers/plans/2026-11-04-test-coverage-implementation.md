# C-DSL Test Coverage — Implementation Plan

**Date:** 2026-11-04  
**Subproject:** F — Test Coverage  
**Design:** docs/superpowers/specs/2026-11-04-test-coverage-design.md  
**Status:** Draft

## Task List

### Phase 1: Visualization coverage (visualize.c — 37.3%)

**Task 1 — Add DOT output tests to tests/test_visual.c**
- Test cdsl_rule_to_dot() with simple rule
- Test cdsl_rule_to_dot() with metric rule
- Test NULL handling

### Phase 2: Schema coverage (schema.c — 58.9%)

**Task 2 — Add schema tests to tests/test_schema.c**
- Test schema variable registration with all types
- Test schema action registration
- Test schema lookup (get/set)
- Test NULL-safe operations

### Phase 3: Context coverage (context.c — 60.3%)

**Task 3 — Add context tests**
- Test context variable get/set
- Test context variable types
- Test NULL-safe context operations

### Phase 4: Bytecode coverage (bytecode.c — 62.9%)

**Task 4 — Add bytecode tests to tests/test_bytecode.c**
- Test more opcode paths
- Test error paths in bytecode execution

### Phase 5: Verification

**Task 5 — Build & test**
- Normal build: all tests pass
- ASan build: all tests pass
- Coverage: visualize.c > 60%, schema.c > 70%

**Task 6 — Commit**

## Estimated Effort

~4 hours

## Rollback Plan

`git checkout HEAD -- tests/test_*.c` to restore pre-F state.