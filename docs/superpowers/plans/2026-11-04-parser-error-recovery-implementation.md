# C-DSL Parser Error Recovery — Implementation Plan

**Date:** 2026-11-04  
**Subproject:** E — Parser Error Recovery  
**Design:** docs/superpowers/specs/2026-11-04-parser-error-recovery-design.md  
**Status:** Draft

## Overview

Implement enhanced error recovery in the C-DSL Flex/Bison parser: column tracking, sub-rule error productions, improved rule-level sync, partial results support, and error hints.

## Task List

### Phase 1: AST + Error Infrastructure

**Task 1 — Add CDSL_EXPR_ERROR AST node type**
- Add `CDSL_EXPR_ERROR` to `cdsl_expr_type_t` in `include/cdsl/ast.h`
- Add `cdsl_create_expr_error()` declaration in `include/cdsl/ast.h`
- Implement `cdsl_create_expr_error()` in `src/ast/ast.c`
- Add `cdsl_free_expr` handling for `CDSL_EXPR_ERROR` (no children to free)

**Task 2 — Column tracking in lexer**
- Add column counter to lexer (reset on `\n`, increment by `yyleng`)
- Add `yyget_column(yyscan_t)` declaration/implementation
- Report column in lexer-created errors (unknown character)

### Phase 2: Parser Grammar Changes

**Task 3 — Add error productions to sub-rules**
- `expression | error` → `cdsl_create_expr_error()`
- `action_statement | error` → NULL
- `case_item | error` → NULL (skip case)
- `meta_item | error` → NULL (skip meta item)

**Task 4 — Improve rule_declaration error sync**
- Replace `yyerrok; yyclearin;` with token-discard loop syncing to RULE/TEMPLATE/EOF
- Free heap-allocated token values during discard (IDENTIFIER, STRING_LIT, DATE_LIT)

**Task 5 — Enhance yyerror with column + hint**
- Add column via `yyget_column(scanner)`
- Add hints for common error patterns (missing THEN, unexpected }, etc.)

### Phase 3: Parse Bridge (Partial Results)

**Task 6 — Allow partial results in cdsl_parse_string**
- When `errors != NULL` and `error_count > 0` but `rule != NULL`, return the rule with errors
- Preserve existing behavior when `errors == NULL` (return NULL on any error)

### Phase 4: Tests

**Task 7 — New test file: tests/test_parser_errors.c**
- `test_parser_error_missing_then` — missing THEN keyword
- `test_parser_error_extra_brace` — extra closing brace
- `test_parser_error_bad_operator` — invalid operator in expression
- `test_parser_multi_rule_partial` — 2 rules, 1 bad → first still parses with errors
- `test_parser_error_column` — verify line + column in error
- `test_parser_error_hint` — verify hint populated

### Phase 5: Verification

**Task 8 — Build & test**
- Normal build: all tests pass (22 existing + new)
- ASan build: all tests pass, no leaks
- clang-tidy: no new warnings

**Task 9 — Commit**
- Commit all changes with gitmoji

## Estimated Effort

- Tasks 1-2: ~1 hour
- Tasks 3-5: ~2 hours
- Task 6: ~30 min
- Task 7: ~1.5 hours
- Tasks 8-9: ~1 hour
- **Total: ~6 hours**

## Dependencies

- None (builds on Subproject D's rwlock work which is already committed)
- Requires Flex/Bison (already in build system)

## Rollback Plan

If issues arise, `git checkout HEAD -- parser/parser.y parser/lexer.l src/ast/ast.c include/cdsl/ast.h src/ast/parse.c` to restore pre-E state.