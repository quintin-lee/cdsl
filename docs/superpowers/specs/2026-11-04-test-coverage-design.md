# C-DSL Test Coverage Design

**Date:** 2026-11-04  
**Subproject:** F — Test Coverage  
**Status:** Draft  

## 1. Executive Summary

Improve test coverage across the C-DSL codebase, focusing on modules with the lowest coverage. Current coverage (gcov) shows significant gaps in visualization, AI bridge, schema, context, and bytecode modules.

## 2. Current Coverage (baseline)

| File | Coverage |
|------|----------|
| vm/visualize.c | 37.3% |
| ai/bridge.c | 41.6% |
| schema/schema.c | 58.9% |
| vm/context.c | 60.3% |
| vm/bytecode.c | 62.9% |
| vm/eval.c | 67.8% |
| util/json.c | 69.0% |
| vm/ruleset.c | 72.2% |
| ast/template.c | 72.2% |
| vm/codegen.c | 72.9% |
| vm/builtins.c | 76.8% |
| vm/vm.c | 88.3% |
| util/hashmap.c | 85.9% |
| util/error.c | 84.4% |
| util/arena.c | 84.3% |
| ast/ast.c | 81.7% |

## 3. Design Goals

| Goal | Target |
|------|--------|
| G1 | Raise visualize.c coverage from 37% to >60% |
| G2 | Raise schema.c coverage from 59% to >70% |
| G3 | Raise context.c coverage from 60% to >70% |
| G4 | Raise bytecode.c coverage from 63% to >70% |
| G5 | Maintain all existing tests passing |

## 4. Files Affected

| File | Change |
|------|--------|
| tests/test_visual.c | Add DOT output tests for visualize.c |
| tests/test_schema.c | Add schema edge case tests |
| tests/test_context.c | New context variable tests (or extend test_vm_config.c) |
| tests/test_bytecode.c | Add bytecode opcode coverage tests |

## 5. Backward Compatibility

No public API changes. Only adding new test functions.

## 6. Success Criteria

- All 23 existing tests continue to pass
- New tests pass
- visualize.c coverage > 60%
- schema.c coverage > 70%