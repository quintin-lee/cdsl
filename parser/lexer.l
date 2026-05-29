%{
#include "ast.h"
#include "parser.tab.h"
#include <string.h>
#include <stdlib.h>
%}

%option noyywrap
%option yylineno
%option noinput
%option nounput

%x BLOCK_COMMENT

%%

"/*"                    { BEGIN(BLOCK_COMMENT); }
<BLOCK_COMMENT>"*/"     { BEGIN(INITIAL); }
<BLOCK_COMMENT>[^*\n]+  ;
<BLOCK_COMMENT>"*"+     ;
<BLOCK_COMMENT>\n       ;

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

"true"                  { yylval.bool_val = 1; return BOOL_LIT; }
"false"                 { yylval.bool_val = 0; return BOOL_LIT; }

[0-9]+\.[0-9]+          { yylval.float_val = atof(yytext); return FLOAT_LIT; }
[0-9]+                  { yylval.int_val = atoi(yytext); return INT_LIT; }

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
                            yylval.string_val = s;
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

"+"                     { return PLUS; }
"-"                     { return MINUS; }
"*"                     { return STAR; }
"/"                     { return SLASH; }

"{"                      { return LBRACE; }
"}"                      { return RBRACE; }
"("                      { return LPAREN; }
")"                      { return RPAREN; }
"="                      { return ASSIGN; }
","                      { return COMMA; }

[ \t\n\r]+              ;
.                       { printf("Unknown token: %s\n", yytext); }

%%
