/**
 * @file lexer.l
 * @brief Flex lexical analyzer for the C-DSL language.
 *
 * This lexer tokenizes C-DSL source input for the Bison-generated parser.
 * It supports:
 * - C-style block comments (slash-asterisk ... asterisk-slash) and line comments (// ...)
 * - DSL keywords: RULE, META, WHEN, THEN, METRIC, CASE, DEFAULT,
 *   TEMPLATE, EXTENDS
 * - Logical operators: AND (&&), OR (||), NOT (!)
 * - Boolean literals: true, false
 * - Numeric literals: integers and floats
 * - String literals with escape sequence processing (\\n, \\t, \\r, \\", \\\\)
 * - Identifiers with dots (e.g. "user.age")
 * - All comparison and arithmetic operators
 *
 * Uses a reentrant (thread-safe) scanner mode for compatibility
 * with concurrent rule parsing.
 */

%{
#include "cdsl/ast.h"
#include "cdsl/util/error.h"
#include "cdsl/util/portability.h"
#include "parser.tab.h"
#include <string.h>
#include <stdlib.h>

/* Forward declaration for yylex (defined via YY_DECL below) */
int yylex(YYSTYPE * yylval_param, YYLTYPE * yylloc, yyscan_t yyscanner);

#ifdef _WIN32
#include <io.h>
#define isatty _isatty
#define fileno _fileno
#else
#include <unistd.h>
#endif

/* Column tracking: maintained manually because Flex's built-in yycolumn
   is unreliable in reentrant mode. */
#undef yycolumn
THREAD_LOCAL int yycolumn = 0;

void cdsl_lexer_reset_column(void);
void cdsl_lexer_reset_column(void) {
	yycolumn = 0;
}

#define YY_USER_ACTION                                                                             \
	do {                                                                                       \
		if (yylloc_param) {                                                                \
			yylloc_param->first_line = yylineno;                                       \
			yylloc_param->last_line = yylineno;                                        \
			yylloc_param->first_column = yycolumn + 1;                                 \
			yylloc_param->last_column = yycolumn + (int)yyleng;                        \
		}                                                                                  \
		for (int _i = 0; _i < (int)yyleng; _i++) {                                         \
			if (yytext[_i] == '\n') {                                                  \
				yycolumn = 0;                                                      \
			} else {                                                                   \
				yycolumn++;                                                        \
			}                                                                          \
		}                                                                                  \
	} while (0);

/* Override YY_DECL to match bison's 3-arg yylex call (with YYLTYPE* for location).
   All parameters must be named: unnamed parameters in a definition are C23-only
   and rejected by MSVC (C2055). */
#undef YY_DECL
#define YY_DECL int yylex(YYSTYPE * yylval_param, YYLTYPE * yylloc_param, yyscan_t yyscanner)
%}

%option noyywrap
%option yylineno
%option noinput
%option nounput
%option reentrant
%option bison-bridge
%option extra-type="struct cdsl_error_list*"
%option nounistd

%x BLOCK_COMMENT

%%
	(void)yylloc_param; /* location is tracked via yycolumn, not yylloc */

"/*"                    { BEGIN(BLOCK_COMMENT); }
<BLOCK_COMMENT>"*/"     { BEGIN(INITIAL); }
<BLOCK_COMMENT>[^*\n]+  ;
<BLOCK_COMMENT>"*"+     ;
<BLOCK_COMMENT>\n       ;
<BLOCK_COMMENT><<EOF>>  { return 0; }

"//"[^\n]*              ;

"RULE"                  { return RULE; }
"META"                  { return META; }
"WHEN"                  { return WHEN; }
"THEN"                  { return THEN; }
"METRIC"                { return METRIC; }
"CASE"                  { return CASE; }
"DEFAULT"               { return DEFAULT; }
"TEMPLATE"              { return TEMPLATE; }
"EXTENDS"               { return EXTENDS; }

"AND"|&&                { return AND; }
"OR"|\|\|               { return OR; }
"NOT"|"!"               { return NOT; }

"true"                  { yylval->bool_val = 1; return BOOL_LIT; }
"false"                 { yylval->bool_val = 0; return BOOL_LIT; }

[0-9]+L                 { yylval->long_val = strtoll(yytext, NULL, 10); return LONG_LIT; }
[0-9]+\.[0-9]+          { yylval->float_val = strtod(yytext, NULL); return FLOAT_LIT; }
[0-9]+                  { yylval->int_val = (int)strtol(yytext, NULL, 10); return INT_LIT; }

\"([^\"]|\\.)*\"          {
                            size_t slen = strlen(yytext);
                            char* s = malloc(slen);
                            size_t j = 0;
                            for (size_t i = 1; i < slen - 1; i++) {
                                if (yytext[i] == '\\' && i + 1 < slen - 1) {
                                    switch (yytext[i + 1]) {
                                    case 'n': s[j++] = '\n'; i++; break;
                                    case 't': s[j++] = '\t'; i++; break;
                                    case 'r': s[j++] = '\r'; i++; break;
                                    case '"': s[j++] = '"';  i++; break;
                                    case '\\':s[j++] = '\\'; i++; break;
                                    default:  s[j++] = yytext[i]; break;
                                    }
                                } else {
                                    s[j++] = yytext[i];
                                }
                            }
                            s[j] = '\0';
                            yylval->string_val = s;
                            return STRING_LIT;
                        }

[a-zA-Z_][a-zA-Z0-9_\.]* {
                            yylval->id_val = strdup(yytext);
                            return IDENTIFIER;
                        }

@[0-9]{4}-[0-9]{2}-[0-9]{2}([ ]+[0-9]{2}:[0-9]{2}:[0-9]{2})? {
                            yylval->string_val = strdup(yytext + 1);
                            return DATE_LIT;
                        }

"=="                    { return EQ; }
"!="                    { return NE; }
"<="                    { return LE; }
">="                    { return GE; }
"<"                     { return LT; }
">"                     { return GT; }

"+"                     { return PLUS; }
"-"                     { return MINUS; }
"*"                     { return STAR; }
"/"                     { return SLASH; }

"{"                      { return LBRACE; }
"}"                      { return RBRACE; }
"["                      { return LBRACKET; }
"]"                      { return RBRACKET; }
"("                      { return LPAREN; }
")"                      { return RPAREN; }
"="                      { return ASSIGN; }
","                      { return COMMA; }

[ \t\n\r]+              ;
.                       {
                            if (yyextra) {
                                char buf[128];
                                snprintf(buf, sizeof(buf), "Unknown character: '%s'", yytext);
                                int safe_line = yylineno;
                                if (safe_line < 0) safe_line = 0;
                                int safe_col = yylloc_param ? yylloc_param->first_column : yycolumn;
                                cdsl_error_t* err = cdsl_error_create(CDSL_ERR_SYNTAX, safe_line, safe_col, buf, NULL);
                                cdsl_error_list_add(yyextra, err);
                            }
                        }

%%
