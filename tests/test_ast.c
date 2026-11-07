/**
 * @file test_ast.c
 * @brief Unit tests for the C-DSL AST construction and parsing.
 *
 * Tests expression constructors (int, string, binary), action creation,
 * metadata list management, simple rule creation, and DSL input length
 * limit enforcement.
 */

#include "test.h"
#include "cdsl/ast.h"
#include <stdlib.h>

/**
 * @brief Test integer literal expression creation.
 */
void
test_ast_create_expr(void)
{
	TEST_BEGIN("create int expression");
	cdsl_expr_node_t* e = cdsl_create_expr_int(42, 0, 0);
	TEST_ASSERT_NOT_NULL(e, "expr not null");
	TEST_ASSERT_INT(e->type, CDSL_EXPR_INT, "expr type is INT");
	TEST_ASSERT_INT(e->data.int_val, 42, "expr value is 42");
	cdsl_free_expr(e);
	TEST_END();
}

/**
 * @brief Test string literal expression creation.
 */
void
test_ast_create_string(void)
{
	TEST_BEGIN("create string expression");
	cdsl_expr_node_t* e = cdsl_create_expr_string(strdup("hello"), 0, 0);
	TEST_ASSERT_NOT_NULL(e, "expr not null");
	TEST_ASSERT_STR(e->data.string_val, "hello", "string value");
	cdsl_free_expr(e);
	TEST_END();
}

/**
 * @brief Test binary operation expression creation (GT comparison).
 */
void
test_ast_create_binary(void)
{
	TEST_BEGIN("create binary expression");
	cdsl_expr_node_t* l = cdsl_create_expr_int(10, 0, 0);
	cdsl_expr_node_t* r = cdsl_create_expr_int(20, 0, 0);
	cdsl_expr_node_t* b = cdsl_create_expr_binary(CDSL_OP_GT, l, r, 0, 0);
	TEST_ASSERT_NOT_NULL(b, "binary not null");
	TEST_ASSERT_INT(b->type, CDSL_EXPR_BINARY, "type is BINARY");
	TEST_ASSERT_INT(b->data.binary.op, CDSL_OP_GT, "op is GT");
	cdsl_free_expr(b);
	TEST_END();
}

/**
 * @brief Test action node creation with arguments.
 */
void
test_ast_create_action(void)
{
	TEST_BEGIN("create action with args");
	cdsl_expr_node_t* e = cdsl_create_expr_string(strdup("reason"), 0, 0);
	cdsl_arg_node_t* arg = cdsl_create_arg(e);
	cdsl_action_node_t* a = cdsl_create_action(strdup("reject"), arg, 0, 0);
	TEST_ASSERT_NOT_NULL(a, "action not null");
	TEST_ASSERT_STR(a->action_name, "reject", "action name");
	TEST_ASSERT_NOT_NULL(a->args, "args not null");
	cdsl_free_action(a);
	TEST_END();
}

/**
 * @brief Test metadata list creation and lookup.
 */
void
test_ast_create_meta(void)
{
	TEST_BEGIN("create meta list");
	cdsl_meta_item_t* m1 = cdsl_create_meta_item(strdup("key1"), strdup("val1"));
	cdsl_meta_item_t* m2 = cdsl_create_meta_item(strdup("key2"), strdup("val2"));
	cdsl_meta_item_t* list = cdsl_append_meta(m1, m2);
	TEST_ASSERT_NOT_NULL(list, "list not null");
	TEST_ASSERT_STR(cdsl_meta_get(list, "key1"), "val1", "find key1");
	TEST_ASSERT_STR(cdsl_meta_get(list, "key2"), "val2", "find key2");
	TEST_ASSERT_NULL(cdsl_meta_get(list, "key3"), "key3 not found");
	cdsl_free_meta(list);
	TEST_END();
}

/**
 * @brief Test simple rule creation and field verification.
 */
void
test_ast_create_rule(void)
{
	TEST_BEGIN("create simple rule");
	cdsl_meta_item_t* meta = cdsl_create_meta_item(strdup("description"), strdup("test rule"));
	cdsl_expr_node_t* when = cdsl_create_expr_bool(1, 0, 0);
	cdsl_action_node_t* then = cdsl_create_action(strdup("block"), NULL, 0, 0);
	cdsl_rule_t* rule = cdsl_create_simple_rule(strdup("test_rule"), meta, when, then, 0, 0);
	TEST_ASSERT_NOT_NULL(rule, "rule not null");
	TEST_ASSERT_STR(rule->name, "test_rule", "rule name");
	TEST_ASSERT_NOT_NULL(rule->when_expr, "when expr");
	cdsl_free_rule(rule);
	TEST_END();
}

/**
 * @brief Test that overly long DSL input is rejected.
 */
void
test_ast_parse_limit(void)
{
	TEST_BEGIN("parse string maximum length limit");
	char* large_input = malloc(70000);
	TEST_ASSERT_NOT_NULL(large_input, "malloc large_input");
	if (large_input) {
		memset(large_input, 'A', 69999);
		large_input[69999] = '\0';
		cdsl_rule_t* r = cdsl_parse_string(large_input, NULL);
		TEST_ASSERT_NULL(r, "parse too large string returns NULL");
		free(large_input);
	}
	TEST_END();
}

/**
 * @brief Test operator predicates, string mappers, and type names.
 */
static void
test_ast_operator_and_type_contracts(void)
{
	TEST_BEGIN("operator and type contracts");

	/* Relational operators */
	TEST_ASSERT_INT(cdsl_op_is_relational(CDSL_OP_EQ), 1, "EQ is relational");
	TEST_ASSERT_INT(cdsl_op_is_relational(CDSL_OP_NE), 1, "NE is relational");
	TEST_ASSERT_INT(cdsl_op_is_relational(CDSL_OP_LT), 1, "LT is relational");
	TEST_ASSERT_INT(cdsl_op_is_relational(CDSL_OP_GT), 1, "GT is relational");
	TEST_ASSERT_INT(cdsl_op_is_relational(CDSL_OP_LE), 1, "LE is relational");
	TEST_ASSERT_INT(cdsl_op_is_relational(CDSL_OP_GE), 1, "GE is relational");
	TEST_ASSERT_INT(cdsl_op_is_relational(CDSL_OP_ADD), 0, "ADD is not relational");
	TEST_ASSERT_INT(cdsl_op_is_relational((cdsl_op_t)999), 0, "invalid op is not relational");

	/* Arithmetic binary operators */
	TEST_ASSERT_INT(cdsl_op_is_arithmetic(CDSL_OP_ADD), 1, "ADD is arithmetic");
	TEST_ASSERT_INT(cdsl_op_is_arithmetic(CDSL_OP_SUB), 1, "SUB is arithmetic");
	TEST_ASSERT_INT(cdsl_op_is_arithmetic(CDSL_OP_MUL), 1, "MUL is arithmetic");
	TEST_ASSERT_INT(cdsl_op_is_arithmetic(CDSL_OP_DIV), 1, "DIV is arithmetic");
	TEST_ASSERT_INT(cdsl_op_is_arithmetic(CDSL_OP_EQ), 0, "EQ is not arithmetic");

	/* Logical binary operators */
	TEST_ASSERT_INT(cdsl_op_is_logical_binary(CDSL_OP_AND), 1, "AND is logical binary");
	TEST_ASSERT_INT(cdsl_op_is_logical_binary(CDSL_OP_OR), 1, "OR is logical binary");
	TEST_ASSERT_INT(cdsl_op_is_logical_binary(CDSL_OP_NOT), 0, "NOT is unary not binary");

	/* Unary operators */
	TEST_ASSERT_INT(cdsl_op_is_unary(CDSL_OP_NOT), 1, "NOT is unary");
	TEST_ASSERT_INT(cdsl_op_is_unary(CDSL_OP_NEG), 1, "NEG is unary");
	TEST_ASSERT_INT(cdsl_op_is_unary(CDSL_OP_ADD), 0, "ADD is not unary");

	/* C code symbols */
	TEST_ASSERT_STR(cdsl_op_c_symbol(CDSL_OP_EQ), "==", "symbol EQ");
	TEST_ASSERT_STR(cdsl_op_c_symbol(CDSL_OP_AND), "&&", "symbol AND");
	TEST_ASSERT_STR(cdsl_op_c_symbol(CDSL_OP_OR), "||", "symbol OR");
	TEST_ASSERT_STR(cdsl_op_c_symbol(CDSL_OP_NOT), "!", "symbol NOT");
	TEST_ASSERT_STR(cdsl_op_c_symbol(CDSL_OP_NEG), "-", "symbol NEG");
	TEST_ASSERT_STR(cdsl_op_c_symbol((cdsl_op_t)999), "?", "invalid symbol ?");

	/* DOT/friendly names */
	TEST_ASSERT_STR(cdsl_op_name(CDSL_OP_EQ), "==", "name EQ");
	TEST_ASSERT_STR(cdsl_op_name(CDSL_OP_AND), "AND", "name AND");
	TEST_ASSERT_STR(cdsl_op_name(CDSL_OP_OR), "OR", "name OR");
	TEST_ASSERT_STR(cdsl_op_name(CDSL_OP_NOT), "NOT", "name NOT");
	TEST_ASSERT_STR(cdsl_op_name(CDSL_OP_NEG), "NEG", "name NEG");
	TEST_ASSERT_STR(cdsl_op_name((cdsl_op_t)999), "?", "invalid name ?");

	/* Type names */
	TEST_ASSERT_STR(cdsl_type_name(CDSL_TYPE_INT), "INT", "type INT");
	TEST_ASSERT_STR(cdsl_type_name(CDSL_TYPE_FLOAT), "FLOAT", "type FLOAT");
	TEST_ASSERT_STR(cdsl_type_name(CDSL_TYPE_BOOL), "BOOL", "type BOOL");
	TEST_ASSERT_STR(cdsl_type_name(CDSL_TYPE_STRING), "STRING", "type STRING");
	TEST_ASSERT_STR(cdsl_type_name(CDSL_TYPE_DATE), "DATE", "type DATE");
	TEST_ASSERT_STR(cdsl_type_name(CDSL_TYPE_LONG), "LONG", "type LONG");
	TEST_ASSERT_STR(cdsl_type_name(CDSL_TYPE_ARRAY), "ARRAY", "type ARRAY");
	TEST_ASSERT_STR(cdsl_type_name(CDSL_TYPE_VOID), "VOID", "type VOID");
	TEST_ASSERT_STR(cdsl_type_name((cdsl_type_t)999), "UNKNOWN", "invalid type UNKNOWN");

	/* Unified operator descriptor table */
	const cdsl_op_desc_t* desc_eq = cdsl_op_get_desc(CDSL_OP_EQ);
	TEST_ASSERT(desc_eq != NULL, "desc EQ exists");
	TEST_ASSERT_INT(desc_eq->op, CDSL_OP_EQ, "desc EQ op matches");
	TEST_ASSERT_STR(desc_eq->c_symbol, "==", "desc EQ c_symbol");
	TEST_ASSERT_STR(desc_eq->name, "==", "desc EQ name");
	TEST_ASSERT_INT(desc_eq->arity, 2, "desc EQ arity");
	TEST_ASSERT_INT(desc_eq->is_relational, 1, "desc EQ is_relational");
	TEST_ASSERT_INT(desc_eq->is_arithmetic, 0, "desc EQ is_arithmetic");
	TEST_ASSERT_INT(desc_eq->is_logical, 0, "desc EQ is_logical");

	const cdsl_op_desc_t* desc_not = cdsl_op_get_desc(CDSL_OP_NOT);
	TEST_ASSERT(desc_not != NULL, "desc NOT exists");
	TEST_ASSERT_STR(desc_not->c_symbol, "!", "desc NOT c_symbol");
	TEST_ASSERT_STR(desc_not->name, "NOT", "desc NOT name");
	TEST_ASSERT_INT(desc_not->arity, 1, "desc NOT arity");
	TEST_ASSERT_INT(desc_not->is_logical, 1, "desc NOT is_logical");

	const cdsl_op_desc_t* desc_add = cdsl_op_get_desc(CDSL_OP_ADD);
	TEST_ASSERT(desc_add != NULL, "desc ADD exists");
	TEST_ASSERT_STR(desc_add->c_symbol, "+", "desc ADD c_symbol");
	TEST_ASSERT_INT(desc_add->arity, 2, "desc ADD arity");
	TEST_ASSERT_INT(desc_add->is_arithmetic, 1, "desc ADD is_arithmetic");

	/* Out-of-bounds operator query */
	TEST_ASSERT(cdsl_op_get_desc((cdsl_op_t)-1) == NULL, "negative op returns NULL");
	TEST_ASSERT(cdsl_op_get_desc((cdsl_op_t)CDSL_OP_COUNT) == NULL,
		    "CDSL_OP_COUNT returns NULL");
	TEST_ASSERT(cdsl_op_get_desc((cdsl_op_t)999) == NULL, "invalid op 999 returns NULL");

	TEST_END();
}

/**
 * @brief Main entry: run all AST test cases.
 * @return 0 if all tests passed, 1 otherwise
 */
int
main(void)
{
	printf("========================================\n");
	printf("  AST Unit Tests\n");
	printf("========================================\n");

	test_ast_create_expr();
	test_ast_create_string();
	test_ast_create_binary();
	test_ast_create_action();
	test_ast_create_meta();
	test_ast_create_rule();
	test_ast_parse_limit();
	test_ast_operator_and_type_contracts();

	TEST_SUMMARY();
	TEST_EXIT();
}
