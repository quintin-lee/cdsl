# C-DSL Parser Error Recovery Design

**Date:** 2026-11-04  
**Subproject:** E — Parser Error Recovery  
**Status:** Draft  
**Author:** C-DSL Development Team

## 1. Executive Summary

The C-DSL parser (Flex/Bison) currently has minimal error recovery: a single `error` production at the `rule_declaration` level that uses `yyerrok` + `yyclearin` to skip the offending token and continue. When a syntax error occurs deep inside an expression, action, or case clause, Bison unwinds the parse stack to the nearest `error` rule, but the recovery is coarse-grained and often causes cascading failures that prevent subsequent valid rules from being parsed.

This design specifies enhanced error recovery at multiple grammar levels, improved error location reporting (line + column), and a more resilient top-level parse loop that collects all errors across multiple rules rather than failing fast.

## 2. Current State

### 2.1 Error Reporting

- `yyerror()` creates a `cdsl_error_t` with `CDSL_ERR_SYNTAX`, line number, message, and no hint/column.
- `cdsl_error_list_t` accumulates errors during parsing.
- `cdsl_parse_string()` returns NULL if `yyparse()` returns non-zero OR `error_count > 0`, discarding any partially parsed rules.

### 2.2 Error Recovery

Only one `error` production exists, at `rule_declaration`:

```
rule_declaration:
    ...
    | error {
        if (yychar == IDENTIFIER) { free(yylval.id_val); }
        ...
        (*error_count)++; yyerrok; yyclearin; $$ = NULL;
    }
```

No recovery exists for `expression`, `action_statement`, `case_item`, `meta_item`, or `metric_item`.

### 2.3 Limitations

1. **No column information**: Errors report line but not column.
2. **No hint/suggestion**: `yyerror` passes `NULL` for hint.
3. **No sub-rule recovery**: Errors inside expressions/actions abort the entire rule_declaration.
4. **Fail-fast at top level**: `cdsl_parse_string()` returns NULL if any error occurs, even if some rules parsed successfully.
5. **Limited sync**: After `yyerrok; yyclearin`, Bison may immediately hit another error on the next token, causing a cascade.

## 3. Design Goals

| Goal | Description |
|------|-------------|
| G1 | Add column tracking to error reporting |
| G2 | Add `error` productions at `expression`, `action_statement`, `case_item`, `meta_item` levels |
| G3 | Improve `rule_declaration` error recovery to sync to next `RULE`/`TEMPLATE`/EOF |
| G4 | Allow `cdsl_parse_string()` to return partially parsed rules alongside errors |
| G5 | Provide error hints where actionable (e.g., missing `THEN`, unexpected `}`) |
| G6 | Maintain backward compatibility: `errors == NULL` callers still get NULL on any error |

## 4. Design Details

### 4.1 Column Tracking (G1)

Flex's `yylineno` tracks lines. We add `yyget_column()` or track column via `yyleng` (token length) accumulated in a scanner column counter. Simpler: use `yyget_lineno()` for line and add a column computed from `yytext` position within the line.

Implementation: Add a scanner-level column counter reset at each `\n`, incremented by `yyleng` for each token. Expose via `yyget_column(yyscanner)`.

### 4.2 Sub-rule Error Productions (G2)

Add `error` alternatives to these grammar rules:

**expression:**
```
expression:
    ...
    | error { $$ = cdsl_create_expr_error(); }
```

**action_statement:**
```
action_statement:
    IDENTIFIER LPAREN argument_list RPAREN { ... }
    | error { $$ = cdsl_create_action_error(); }
```

**case_item:**
```
case_item:
    CASE expression THEN action_statement { ... }
    | error { $$ = NULL; }  /* Skip this case */
```

**meta_item:**
```
meta_item:
    IDENTIFIER ASSIGN STRING_LIT { ... }
    | error { $$ = NULL; }  /* Skip this meta item */
```

New AST node types: `CDSL_EXPR_ERROR` and action/meta error markers, so the rest of the pipeline can gracefully handle them.

### 4.3 Rule-Level Recovery Sync (G3)

Improve `rule_declaration` error rule:

```
rule_declaration:
    ...
    | error {
        /* Discard tokens until sync point: RULE, TEMPLATE, or EOF */
        while (yychar != 0 && yychar != RULE && yychar != TEMPLATE) {
            int tok = yylex(&yylval, scanner);
            /* free any heap-allocated token values */
            ...
        }
        yyerrok;
        (*error_count)++;
        $$ = NULL;
    }
```

### 4.4 Partial Results (G4)

Modify `cdsl_parse_string()`:
- If `yyparse()` succeeds but `error_count > 0`, return the parsed rule(s) AND populate the error list.
- If caller passes `errors == NULL`, preserve existing behavior (return NULL on any error).
- If caller passes `errors != NULL`, return partial results with errors.

Since `program` can produce multiple rules via `program rule_declaration`, we need to handle a list of rules. However, the current API returns a single `cdsl_rule_t*`. For now, we return the last successfully parsed rule and collect errors in the list.

### 4.5 Error Hints (G5)

Enhance `yyerror` to accept an optional hint. For common patterns:
- Missing `THEN` after expression → hint: "Expected 'THEN' after condition"
- Unexpected `}` → hint: "Extra closing brace; check for missing action"
- Unknown character (from lexer) → hint: "Check for typo or unsupported symbol"

## 5. AST Changes

### 5.1 New Expression Node Type

Add `CDSL_EXPR_ERROR` to `cdsl_expr_type_t` in `include/cdsl/ast.h`:

```c
typedef enum {
    ...
    CDSL_EXPR_ERROR,   /**< Error placeholder in expression position */
} cdsl_expr_type_t;
```

Add factory: `cdsl_create_expr_error()` returning an error expression node.

### 5.2 Error Action/ Meta Markers

For `action_statement` and `meta_item` errors, return NULL and let callers handle NULL gracefully (existing code already NULL-checks most AST children).

## 6. Files Affected

| File | Change |
|------|--------|
| `include/cdsl/ast.h` | Add `CDSL_EXPR_ERROR` type + `cdsl_create_expr_error()` |
| `src/ast/ast.c` | Implement `cdsl_create_expr_error()` |
| `include/cdsl/util/error.h` | (No change — already has line/column/hint fields) |
| `parser/lexer.l` | Add column tracking; expose `yyget_column()` |
| `parser/parser.y` | Add `error` productions; improve `rule_declaration` sync; enhance `yyerror` with column + hint |
| `src/ast/parse.c` | Allow partial results when `errors != NULL` |
| `tests/test_parser_errors.c` | New test file for error recovery scenarios |

## 7. Backward Compatibility

- `cdsl_parse_string(dsl, NULL)` → unchanged: returns NULL on any error.
- `cdsl_parse_string(dsl, &errs)` → now returns partial rule on success-with-errors, populates `errs`.
- Public API signatures unchanged.
- AST node enum extended (additive, existing code unaffected).

## 8. Test Plan

| Test | Description |
|------|-------------|
| `test_parser_error_missing_then` | `RULE r { WHEN x > 5 block("y") }` — missing THEN, expect error + recovery |
| `test_parser_error_extra_brace` | `RULE r { WHEN x > 5 THEN block("y") } }` — extra closing brace |
| `test_parser_error_bad_expr` | `RULE r { WHEN x @@ 5 THEN block("y") }` — invalid operator |
| `test_parser_multi_rule_partial` | Two rules, second has error — first should still parse when `errors != NULL` |
| `test_parser_error_column` | Verify error has correct line + column |
| `test_parser_error_hint` | Verify error hint is populated for known patterns |

## 9. Success Criteria

- [ ] All 22 existing tests continue to pass
- [ ] New parser error tests pass (target: 8-10 new tests)
- [ ] ASan clean (no leaks from error recovery token discarding)
- [ ] `cdsl_parse_string(dsl, NULL)` behavior unchanged
- [ ] `cdsl_parse_string(dsl, &errs)` returns partial results with errors on multi-rule input with one bad rule