/**
 * @file src/ast/parse.c
 * @brief DSL parser bridge using Flex/Bison-generated parser.
 *
 * @ingroup cdsl_ast
 * @defgroup cdsl_parse Parser bridge implementation
 * @{
 */

#include "cdsl/ast.h"
#include "cdsl/util/error.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Flex/Bison reentrant scanner forward declarations */
typedef void* yyscan_t;
struct yy_buffer_state;
int yylex_init_extra(struct cdsl_error_list* extra, yyscan_t* scanner);
int yylex_destroy(yyscan_t scanner);
struct yy_buffer_state* yy_scan_string(const char*, yyscan_t);
void yy_delete_buffer(struct yy_buffer_state*, yyscan_t);
void yyset_lineno(int _line_number, yyscan_t yyscanner);
void cdsl_lexer_reset_column(void);
int yyparse(yyscan_t scanner,
	    cdsl_rule_t** rule_ptr,
	    int* error_count,
	    struct cdsl_error_list* error_list);

cdsl_rule_t*
cdsl_parse_string(const char* dsl_code, cdsl_error_list_t** errors)
{
	if (!dsl_code) {
		return NULL;
	}
	if (strlen(dsl_code) >= CDSL_MAX_INPUT_LENGTH) {
		if (errors && !*errors) {
			*errors = cdsl_error_list_create();
		}
		if (errors && *errors) {
			char buf[128];
			snprintf(buf,
				 sizeof(buf),
				 "DSL input length (%zu) exceeds maximum allowed (%d)",
				 strlen(dsl_code),
				 CDSL_MAX_INPUT_LENGTH);
			cdsl_error_t* err = cdsl_error_create(CDSL_ERR_SYNTAX, 0, 0, buf, NULL);
			cdsl_error_list_add(*errors, err);
		}
		return NULL;
	}

	yyscan_t scanner;
	cdsl_error_list_t* error_list = NULL;
	int error_count = 0;

	/* If caller wants errors, create or use the provided list */
	if (errors) {
		if (*errors) {
			error_list = *errors;
		} else {
			error_list = cdsl_error_list_create();
		}
	}

	if (yylex_init_extra(error_list, &scanner) != 0) {
		if (errors && !*errors) {
			cdsl_error_list_free(error_list);
		}
		return NULL;
	}

	cdsl_rule_t* rule = NULL;

	/* Create arena for AST node allocations during parsing */
	cdsl_arena_t* arena = cdsl_arena_create(8192);
	cdsl_ast_set_current_arena(arena);
	cdsl_lexer_reset_column();

	struct yy_buffer_state* buf = yy_scan_string(dsl_code, scanner);
	if (buf) {
		yyset_lineno(1, scanner);
	}

	if (yyparse(scanner, &rule, &error_count, error_list) != 0 || error_count > 0) {
		if (errors) {
			/* Caller wants errors: keep partial rule if any, report errors via list.
			 * A partial rule that survives a parse error keeps ownership of the
			 * arena that allocated its AST nodes. Attach that arena to rule->arena
			 * (if the parser did not already) so that cdsl_free_rule(rule) reclaims
			 * it safely, and null the local so the fallback free below is skipped.
			 * Callers MUST free a returned partial rule via cdsl_free_rule(), not
			 * via fine-grained cdsl_free_expr(), because the thread-local arena is
			 * reset below and per-node arena memory is no longer reachable. */
			if (rule) {
				if (!rule->arena) {
					rule->arena = arena;
				}
				arena = NULL;
			}
			/* rule == NULL: nothing to keep; arena is freed by the fallback below */
		} else {
			/* Caller doesn't want errors: fail fast, discard partial results */
			if (rule) {
				cdsl_free_rule(rule);
				rule = NULL;
				arena = NULL;
			} else {
				cdsl_arena_free(arena);
				arena = NULL;
			}
		}
	}

	if (!rule && arena) {
		cdsl_arena_free(arena);
		arena = NULL;
	}

	/* Reset arena context */
	cdsl_ast_set_current_arena(NULL);

	yy_delete_buffer(buf, scanner);
	yylex_destroy(scanner);

	if (errors) {
		*errors = error_list;
	} else if (error_list) {
		/* Caller didn't want errors; discard */
		cdsl_error_list_free(error_list);
	}

	return rule;
}
/** @} */
