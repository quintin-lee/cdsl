%{
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ast.h"

void yyerror(const char *s);
extern int yylineno;
extern int yylex();

cdsl_rule_t* final_parsed_rule = NULL;
int yy_error_count = 0;
%}

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
%token <id_val> IDENTIFIER
%token <int_val> INT_LIT
%token <float_val> FLOAT_LIT
%token <bool_val> BOOL_LIT
%token <string_val> STRING_LIT

%left OR
%left AND
%right NOT
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
    rule_declaration { final_parsed_rule = $1; }
    | template_declaration { final_parsed_rule = $1; }
    | program rule_declaration { final_parsed_rule = $2; }
    | program template_declaration { final_parsed_rule = $2; }
    ;

rule_declaration:
    RULE IDENTIFIER '{' meta_block WHEN expression THEN action_statement '}' {
        $$ = cdsl_create_simple_rule($2, $4, $6, $8);
    }
    | RULE IDENTIFIER '{' meta_block metric_list '}' {
        $$ = cdsl_create_metric_rule($2, $4, $5);
    }
    | RULE IDENTIFIER EXTENDS IDENTIFIER '{' meta_block '}' {
        $$ = cdsl_create_extends_rule($2, $4, $6);
    }
    | error { yy_error_count++; yyerrok; yyclearin; $$ = NULL; }
    ;

template_declaration:
    TEMPLATE IDENTIFIER '{' meta_block metric_list '}' {
        $$ = cdsl_create_metric_rule($2, $4, $5);
        if ($$) {
            cdsl_meta_item_t* m = cdsl_create_meta_item(strdup("__is_template"), strdup("true"));
            $$->meta_list = cdsl_append_meta($$->meta_list, m);
        }
    }
    ;

meta_block:
    META '{' meta_list '}' { $$ = $3; }
    | /* empty */          { $$ = NULL; }
    ;

meta_list:
    meta_item { $$ = $1; }
    | meta_list meta_item { $$ = cdsl_append_meta($1, $2); }
    ;

meta_item:
    IDENTIFIER '=' STRING_LIT {
        $$ = cdsl_create_meta_item($1, $3);
    }
    ;

metric_list:
    metric_item { $$ = $1; }
    | metric_list metric_item { $$ = cdsl_append_metric($1, $2); }
    ;

metric_item:
    METRIC IDENTIFIER '{' meta_block case_list DEFAULT action_statement '}' {
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
    | IDENTIFIER '(' argument_list ')'  { $$ = cdsl_create_expr_call($1, $3); }
    | expression EQ expression          { $$ = cdsl_create_expr_binary(CDSL_OP_EQ, $1, $3); }
    | expression NE expression          { $$ = cdsl_create_expr_binary(CDSL_OP_NE, $1, $3); }
    | expression LT expression          { $$ = cdsl_create_expr_binary(CDSL_OP_LT, $1, $3); }
    | expression GT expression          { $$ = cdsl_create_expr_binary(CDSL_OP_GT, $1, $3); }
    | expression LE expression          { $$ = cdsl_create_expr_binary(CDSL_OP_LE, $1, $3); }
    | expression GE expression          { $$ = cdsl_create_expr_binary(CDSL_OP_GE, $1, $3); }
    | expression AND expression         { $$ = cdsl_create_expr_binary(CDSL_OP_AND, $1, $3); }
    | expression OR expression          { $$ = cdsl_create_expr_binary(CDSL_OP_OR, $1, $3); }
    | NOT expression                    { $$ = cdsl_create_expr_unary(CDSL_OP_NOT, $2); }
    | '(' expression ')'               { $$ = $2; }
    ;

action_statement:
    IDENTIFIER '(' argument_list ')' {
        $$ = cdsl_create_action($1, $3);
    }
    ;

argument_list:
    /* empty */                        { $$ = NULL; }
    | argument_list_nonempty           { $$ = $1; }
    ;

argument_list_nonempty:
    expression { $$ = cdsl_create_arg($1); }
    | argument_list_nonempty ',' expression { $$ = cdsl_append_arg($1, $3); }
    ;

%%

void yyerror(const char *s) {
    fprintf(stderr, "Syntax error at line %d: %s\n", yylineno, s);
    yy_error_count++;
}

int yyget_error_count(void) {
    return yy_error_count;
}

void yyreset_error_count(void) {
    yy_error_count = 0;
}
