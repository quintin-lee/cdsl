/**
 * @file src/ast/parse.c
 * @brief DSL parser bridge using Flex/Bison-generated parser.
 *
 * @ingroup cdsl_ast
 * @defgroup cdsl_parse Parser bridge implementation
 * @{
 */

#include "cdsl/ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Flex/Bison reentrant scanner forward declarations */
typedef void* yyscan_t;
struct yy_buffer_state;
int yylex_init(yyscan_t* scanner);
int yylex_destroy(yyscan_t scanner);
struct yy_buffer_state* yy_scan_string(const char*, yyscan_t);
void yy_delete_buffer(struct yy_buffer_state*, yyscan_t);
int yyparse(yyscan_t scanner, cdsl_rule_t** rule_ptr, int* error_count);

cdsl_rule_t*
cdsl_parse_string(const char* dsl_code)
{
	if (!dsl_code) {
		return NULL;
	}
	if (strlen(dsl_code) >= CDSL_MAX_INPUT_LENGTH) {
		fprintf(stderr,
			"Error: DSL input length (%zu) exceeds maximum allowed (%d)\n",
			strlen(dsl_code),
			CDSL_MAX_INPUT_LENGTH);
		return NULL;
	}

	yyscan_t scanner;
	if (yylex_init(&scanner) != 0) {
		return NULL;
	}

	cdsl_rule_t* rule = NULL;
	int error_count = 0;
	struct yy_buffer_state* buf = yy_scan_string(dsl_code, scanner);

	if (yyparse(scanner, &rule, &error_count) != 0 || error_count > 0) {
		if (rule) {
			cdsl_free_rule(rule);
			rule = NULL;
		}
	}

	yy_delete_buffer(buf, scanner);
	yylex_destroy(scanner);

	return rule;
}
/** @} */
