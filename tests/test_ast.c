#include "test.h"
#include "ast.h"
#include <stdlib.h>

void test_ast_create_expr(void) {
    TEST_BEGIN("create int expression");
    cdsl_expr_node_t* e = cdsl_create_expr_int(42);
    TEST_ASSERT_NOT_NULL(e, "expr not null");
    TEST_ASSERT_INT(e->type, CDSL_EXPR_INT, "expr type is INT");
    TEST_ASSERT_INT(e->data.int_val, 42, "expr value is 42");
    cdsl_free_expr(e);
    TEST_END();
}

void test_ast_create_string(void) {
    TEST_BEGIN("create string expression");
    cdsl_expr_node_t* e = cdsl_create_expr_string(strdup("hello"));
    TEST_ASSERT_NOT_NULL(e, "expr not null");
    TEST_ASSERT_STR(e->data.string_val, "hello", "string value");
    cdsl_free_expr(e);
    TEST_END();
}

void test_ast_create_binary(void) {
    TEST_BEGIN("create binary expression");
    cdsl_expr_node_t* l = cdsl_create_expr_int(10);
    cdsl_expr_node_t* r = cdsl_create_expr_int(20);
    cdsl_expr_node_t* b = cdsl_create_expr_binary(CDSL_OP_GT, l, r);
    TEST_ASSERT_NOT_NULL(b, "binary not null");
    TEST_ASSERT_INT(b->type, CDSL_EXPR_BINARY, "type is BINARY");
    TEST_ASSERT_INT(b->data.binary.op, CDSL_OP_GT, "op is GT");
    cdsl_free_expr(b);
    TEST_END();
}

void test_ast_create_action(void) {
    TEST_BEGIN("create action with args");
    cdsl_expr_node_t* e = cdsl_create_expr_string(strdup("reason"));
    cdsl_arg_node_t* arg = cdsl_create_arg(e);
    cdsl_action_node_t* a = cdsl_create_action(strdup("reject"), arg);
    TEST_ASSERT_NOT_NULL(a, "action not null");
    TEST_ASSERT_STR(a->action_name, "reject", "action name");
    TEST_ASSERT_NOT_NULL(a->args, "args not null");
    cdsl_free_action(a);
    TEST_END();
}

void test_ast_create_meta(void) {
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

void test_ast_create_rule(void) {
    TEST_BEGIN("create simple rule");
    cdsl_meta_item_t* meta = cdsl_create_meta_item(strdup("description"), strdup("test rule"));
    cdsl_expr_node_t* when = cdsl_create_expr_bool(1);
    cdsl_action_node_t* then = cdsl_create_action(strdup("block"), NULL);
    cdsl_rule_t* rule = cdsl_create_simple_rule(strdup("test_rule"), meta, when, then);
    TEST_ASSERT_NOT_NULL(rule, "rule not null");
    TEST_ASSERT_STR(rule->name, "test_rule", "rule name");
    TEST_ASSERT_NOT_NULL(rule->when_expr, "when expr");
    cdsl_free_rule(rule);
    TEST_END();
}

int main(void) {
    printf("========================================\n");
    printf("  AST Unit Tests\n");
    printf("========================================\n");

    test_ast_create_expr();
    test_ast_create_string();
    test_ast_create_binary();
    test_ast_create_action();
    test_ast_create_meta();
    test_ast_create_rule();

    TEST_SUMMARY();
    TEST_EXIT();
}
