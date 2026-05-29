%{
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#endif

/* Forward declaration for the reentrant lexer */
typedef void* yyscan_t;
int yylex(void* yylval_param, yyscan_t yyscanner);
void yyerror(yyscan_t scanner, cdsl_rule_t** rule_ptr, int* error_count, const char *s);

/* Helper to get line number from scanner */
extern int yyget_lineno(yyscan_t yyscanner);

%}

%define api.pure full
%lex-param {yyscan_t scanner}
%parse-param {yyscan_t scanner}
%parse-param {cdsl_rule_t** rule_ptr}
%parse-param {int* error_count}

%union {
    int int_val;
    double float_val;
    int bool_val;
    char* string_val;
    char* id_val;
    struct cdsl_expr_node* expr;
    struct cdsl_arg_node* arg_list;
    struct cdsl_action_node* action;
    struct cdsl_meta_item* meta_list;
    struct cdsl_case_node* case_list;
    struct cdsl_metric_node* metric_list;
    struct cdsl_rule* rule;
}

%token RULE META WHEN THEN METRIC CASE DEFAULT TEMPLATE EXTENDS
%token LBRACE RBRACE LPAREN RPAREN ASSIGN COMMA
%token PLUS MINUS STAR SLASH
%token <id_val> IDENTIFIER
%token <int_val> INT_LIT
%token <float_val> FLOAT_LIT
%token <bool_val> BOOL_LIT
%token <string_val> STRING_LIT

%left OR
%left AND
%left PLUS MINUS
%left STAR SLASH
%right NOT UMINUS
%nonassoc EQ NE LT GT LE GE

%type <rule> program rule_declaration template_declaration
%type <meta_list> meta_block meta_list meta_item
%type <expr> expression
%type <arg_list> argument_list argument_list_nonempty
%type <action> action_statement
%type <case_list> case_list case_item
%type <metric_list> metric_list metric_item

%%

program:
    rule_declaration { *rule_ptr = $1; }
    | template_declaration { *rule_ptr = $1; }
    | program rule_declaration { *rule_ptr = $2; }
    | program template_declaration { *rule_ptr = $2; }
    ;

rule_declaration:
    RULE IDENTIFIER LBRACE meta_block WHEN expression THEN action_statement RBRACE {
        $$ = cdsl_create_simple_rule($2, $4, $6, $8);
    }
    | RULE IDENTIFIER LBRACE meta_block metric_list RBRACE {
        $$ = cdsl_create_metric_rule($2, $4, $5);
    }
    | RULE IDENTIFIER EXTENDS IDENTIFIER LBRACE meta_block RBRACE {
        $$ = cdsl_create_extends_rule($2, $4, $6);
    }
    | error { (*error_count)++; yyerrok; yyclearin; $$ = NULL; }
    ;

template_declaration:
    TEMPLATE IDENTIFIER LBRACE meta_block metric_list RBRACE {
        $$ = cdsl_create_metric_rule($2, $4, $5);
        if ($$) {
            cdsl_meta_item_t* m = cdsl_create_meta_item(strdup("__is_template"), strdup("true"));
            $$->meta_list = cdsl_append_meta($$->meta_list, m);
        }
    }
    ;

meta_block:
    META LBRACE meta_list RBRACE { $$ = $3; }
    | /* empty */          { $$ = NULL; }
    ;

meta_list:
    meta_item { $$ = $1; }
    | meta_list meta_item { $$ = cdsl_append_meta($1, $2); }
    ;

meta_item:
    IDENTIFIER ASSIGN STRING_LIT {
        $$ = cdsl_create_meta_item($1, $3);
    }
    ;

metric_list:
    metric_item { $$ = $1; }
    | metric_list metric_item { $$ = cdsl_append_metric($1, $2); }
    ;

metric_item:
    METRIC IDENTIFIER LBRACE meta_block case_list DEFAULT action_statement RBRACE {
        $$ = cdsl_create_metric($2, $4, $5, $7);
    }
    ;

case_list:
    case_item { $$ = $1; }
    | case_list case_item { $$ = cdsl_append_case($1, $2); }
    ;

case_item:
    CASE expression THEN action_statement {
        $$ = cdsl_create_case($2, $4);
    }
    ;

expression:
    IDENTIFIER                          { $$ = cdsl_create_expr_id($1); }
    | INT_LIT                           { $$ = cdsl_create_expr_int($1); }
    | FLOAT_LIT                         { $$ = cdsl_create_expr_float($1); }
    | BOOL_LIT                          { $$ = cdsl_create_expr_bool($1); }
    | STRING_LIT                        { $$ = cdsl_create_expr_string($1); }
    | IDENTIFIER LPAREN argument_list RPAREN  { $$ = cdsl_create_expr_call($1, $3); }
    | expression EQ expression          { $$ = cdsl_create_expr_binary(CDSL_OP_EQ, $1, $3); }
    | expression NE expression          { $$ = cdsl_create_expr_binary(CDSL_OP_NE, $1, $3); }
    | expression LT expression          { $$ = cdsl_create_expr_binary(CDSL_OP_LT, $1, $3); }
    | expression GT expression          { $$ = cdsl_create_expr_binary(CDSL_OP_GT, $1, $3); }
    | expression LE expression          { $$ = cdsl_create_expr_binary(CDSL_OP_LE, $1, $3); }
    | expression GE expression          { $$ = cdsl_create_expr_binary(CDSL_OP_GE, $1, $3); }
    | expression AND expression         { $$ = cdsl_create_expr_binary(CDSL_OP_AND, $1, $3); }
    | expression OR expression          { $$ = cdsl_create_expr_binary(CDSL_OP_OR, $1, $3); }
    | expression PLUS expression        { $$ = cdsl_create_expr_binary(CDSL_OP_ADD, $1, $3); }
    | expression MINUS expression       { $$ = cdsl_create_expr_binary(CDSL_OP_SUB, $1, $3); }
    | expression STAR expression        { $$ = cdsl_create_expr_binary(CDSL_OP_MUL, $1, $3); }
    | expression SLASH expression       { $$ = cdsl_create_expr_binary(CDSL_OP_DIV, $1, $3); }
    | NOT expression                    { $$ = cdsl_create_expr_unary(CDSL_OP_NOT, $2); }
    | MINUS expression %prec UMINUS     { $$ = cdsl_create_expr_unary(CDSL_OP_NEG, $2); }
    | LPAREN expression RPAREN               { $$ = $2; }
    ;

action_statement:
    IDENTIFIER LPAREN argument_list RPAREN {
        $$ = cdsl_create_action($1, $3);
    }
    ;

argument_list:
    /* empty */                        { $$ = NULL; }
    | argument_list_nonempty           { $$ = $1; }
    ;

argument_list_nonempty:
    expression { $$ = cdsl_create_arg($1); }
    | argument_list_nonempty COMMA expression { $$ = cdsl_append_arg($1, $3); }
    ;

%%

void yyerror(yyscan_t scanner, cdsl_rule_t** rule_ptr, int* error_count, const char *s) {
    (void)rule_ptr;
    fprintf(stderr, "Syntax error at line %d: %s\n", yyget_lineno(scanner), s);
    (*error_count)++;
}
