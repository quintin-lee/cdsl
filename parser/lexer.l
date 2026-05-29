%{
#include "ast.h"
#include "parser.tab.h"
#include <string.h>
#include <stdlib.h>
%}

%option noyywrap
%option yylineno

%%

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

"true"                  { yylval.bool_val = 1; return BOOL_LIT; }
"false"                 { yylval.bool_val = 0; return BOOL_LIT; }

[0-9]+\.[0-9]+          { yylval.float_val = atof(yytext); return FLOAT_LIT; }
[0-9]+                  { yylval.int_val = atoi(yytext); return INT_LIT; }

\"[^\"]*\"              {
                            yylval.string_val = strndup(yytext + 1, strlen(yytext) - 2);
                            return STRING_LIT;
                        }

[a-zA-Z_][a-zA-Z0-9_\.]* {
                            yylval.id_val = strdup(yytext);
                            return IDENTIFIER;
                        }

"=="                    { return EQ; }
"!="                    { return NE; }
"<="                    { return LE; }
">="                    { return GE; }
"<"                     { return LT; }
">"                     { return GT; }

[{}()=,]                { return yytext[0]; }

[ \t\n\r]+              ;
.                       { printf("Unknown token: %s\n", yytext); }

%%
