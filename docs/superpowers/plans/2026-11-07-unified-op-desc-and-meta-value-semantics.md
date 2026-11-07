# 统一算子元数据描述与元数据项值语义重构实施计划 (Implementation Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 统一算子集中描述符（`cdsl_op_desc_t`），消除多处 switch-case 重复维护；重构元数据项（`cdsl_meta_item_t`）为纯粹值语义（深拷贝），消除外部调用时的指针所有权混淆与字面量释放崩溃陷阱。

**Architecture:**
1. **单一数据源**: 在 `include/cdsl/ast.h` 声明 `cdsl_op_desc_t` 与 `cdsl_op_get_desc(cdsl_op_t op)`，在 `src/ast/ast.c` 中建立只读静态表 `s_op_table` 并用 `_Static_assert` 约束完整性；原有 6 个算子查询谓词与映射函数收敛为单行委托。
2. **纯值语义**: 重构 `cdsl_create_meta_item(const char* key, const char* value)` 为全深拷贝，新增 `cdsl_append_meta_kv` 便捷助手函数。外部调用者始终保留原字符串所有权，传入字符串字面量安全无忧。
3. **调用端治理**: 迁移 `parser/parser.y`、`src/ast/template.c` 以及测试用例中的样板代码，清除冗余的手工 `strdup`。

**Tech Stack:** C99/C23, Bison 3.8+, CMake, CTest, GCC/Clang (-Wall -Wextra -Werror)

**Worktree 规范:**
- 隔离目录: `.worktrees/unified-op-desc-and-meta`
- 分支名称: `refactor/unified-op-desc-and-meta`

---

## 涉及文件与职责规划

| 文件路径 | 动作 | 职责描述 |
|---|---|---|
| `include/cdsl/ast.h` | 修改 | 定义 `CDSL_OP_COUNT`、声明 `cdsl_op_desc_t`、`cdsl_op_get_desc`；更新 `cdsl_create_meta_item` 签名 (`const char*`) 并声明 `cdsl_append_meta_kv` |
| `src/ast/ast.c` | 修改 | 实现 `s_op_table` 静态数组与 `_Static_assert`，实现 `cdsl_op_get_desc` 并重构 6 个查询函数为委托；实现深拷贝版 `cdsl_create_meta_item` 与 `cdsl_append_meta_kv` |
| `parser/parser.y` | 修改 | 在 `meta_item` 规则中对词法分析器产生的 `$1` 和 `$3` 显式调用 `free`；在 `template_declaration` 中改用 `cdsl_append_meta_kv` |
| `src/ast/template.c` | 修改 | 在 `copy_metric_list` 中改用 `cdsl_append_meta_kv`，移除冗余的手工 `strdup` 与 `free` 逻辑 |
| `tests/test_ast.c` | 修改 | 新增 `cdsl_op_get_desc` 属性与越界断言用例；新增元数据项字面量安全释放与 `cdsl_append_meta_kv` 用例；移除现有旧测试的手工 `strdup` |
| `tests/test_execution.c` | 修改 | 移除元数据提取测试用例中冗余的手工 `strdup` |

---

### Task 1: 算子集中描述符 `cdsl_op_desc_t` 与静态表实现与单测

**Files:**
- Modify: `include/cdsl/ast.h`
- Modify: `src/ast/ast.c`
- Test: `tests/test_ast.c`

- [ ] **Step 1: 在 `tests/test_ast.c` 中编写失败测试**

在 `tests/test_ast.c` 的 `test_ast_operator_and_type_contracts()` 函数中追加对 `cdsl_op_get_desc` 的全覆盖测试：

```c
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
	TEST_ASSERT(cdsl_op_get_desc((cdsl_op_t)CDSL_OP_COUNT) == NULL, "CDSL_OP_COUNT returns NULL");
	TEST_ASSERT(cdsl_op_get_desc((cdsl_op_t)999) == NULL, "invalid op 999 returns NULL");
```

- [ ] **Step 2: 编译测试以确认失败**

运行：`cmake --build build --target test_ast`
预期输出：编译失败，提示 `cdsl_op_desc_t`、`cdsl_op_get_desc`、`CDSL_OP_COUNT` 未声明。

- [ ] **Step 3: 在 `include/cdsl/ast.h` 和 `src/ast/ast.c` 中实现描述符与静态表**

在 `include/cdsl/ast.h` 的 `cdsl_op_t` 枚举后定义描述符结构体与声明：
```c
#define CDSL_OP_COUNT 14

/**
 * @brief 算子元数据集中描述符结构体
 */
typedef struct cdsl_op_desc {
	cdsl_op_t op;		 /**< 算子枚举常量 */
	const char* c_symbol;	 /**< 对应的 C 代码运算符（如 "==", "&&", "!", "+", "-"） */
	const char* name;	 /**< 算子友好名称（如 "==", "AND", "OR", "NOT", "NEG"） */
	int arity;		 /**< 算子目数：1 为一元算子，2 为二元算子 */
	bool is_relational;	 /**< 是否为关系比较算子 (==, !=, <, >, <=, >=) */
	bool is_arithmetic;	 /**< 是否为二元算术算子 (+, -, *, /) */
	bool is_logical;	 /**< 是否为逻辑算子 (AND, OR, NOT) */
} cdsl_op_desc_t;

/**
 * @brief 获取算子对应的只读描述符
 * @param op 算子枚举
 * @return 描述符常量指针；若枚举非法越界则返回 NULL
 */
const cdsl_op_desc_t* cdsl_op_get_desc(cdsl_op_t op);
```

在 `src/ast/ast.c` 中定义 `s_op_table` 并重写原有 6 个函数：
```c
static const cdsl_op_desc_t s_op_table[] = {
    [CDSL_OP_EQ]  = {CDSL_OP_EQ,  "==", "==",  2, true,  false, false},
    [CDSL_OP_NE]  = {CDSL_OP_NE,  "!=", "!=",  2, true,  false, false},
    [CDSL_OP_LT]  = {CDSL_OP_LT,  "<",  "<",   2, true,  false, false},
    [CDSL_OP_GT]  = {CDSL_OP_GT,  ">",  ">",   2, true,  false, false},
    [CDSL_OP_LE]  = {CDSL_OP_LE,  "<=", "<=",  2, true,  false, false},
    [CDSL_OP_GE]  = {CDSL_OP_GE,  ">=", ">=",  2, true,  false, false},
    [CDSL_OP_AND] = {CDSL_OP_AND, "&&", "AND", 2, false, false, true },
    [CDSL_OP_OR]  = {CDSL_OP_OR,  "||", "OR",  2, false, false, true },
    [CDSL_OP_NOT] = {CDSL_OP_NOT, "!",  "NOT", 1, false, false, true },
    [CDSL_OP_ADD] = {CDSL_OP_ADD, "+",  "+",   2, false, true,  false},
    [CDSL_OP_SUB] = {CDSL_OP_SUB, "-",  "-",   2, false, true,  false},
    [CDSL_OP_MUL] = {CDSL_OP_MUL, "*",  "*",   2, false, true,  false},
    [CDSL_OP_DIV] = {CDSL_OP_DIV, "/",  "/",   2, false, true,  false},
    [CDSL_OP_NEG] = {CDSL_OP_NEG, "-",  "NEG", 1, false, false, false},
};

_Static_assert(sizeof(s_op_table) / sizeof(s_op_table[0]) == CDSL_OP_COUNT,
               "Operator table size must match CDSL_OP_COUNT");

const cdsl_op_desc_t*
cdsl_op_get_desc(cdsl_op_t op)
{
	if ((int)op >= 0 && (int)op < CDSL_OP_COUNT) {
		return &s_op_table[op];
	}
	return NULL;
}

bool
cdsl_op_is_relational(cdsl_op_t op)
{
	const cdsl_op_desc_t* d = cdsl_op_get_desc(op);
	return d ? d->is_relational : false;
}

bool
cdsl_op_is_arithmetic(cdsl_op_t op)
{
	const cdsl_op_desc_t* d = cdsl_op_get_desc(op);
	return d ? d->is_arithmetic : false;
}

bool
cdsl_op_is_logical_binary(cdsl_op_t op)
{
	const cdsl_op_desc_t* d = cdsl_op_get_desc(op);
	return d ? (d->is_logical && d->arity == 2) : false;
}

bool
cdsl_op_is_unary(cdsl_op_t op)
{
	const cdsl_op_desc_t* d = cdsl_op_get_desc(op);
	return d ? (d->arity == 1) : false;
}

const char*
cdsl_op_c_symbol(cdsl_op_t op)
{
	const cdsl_op_desc_t* d = cdsl_op_get_desc(op);
	return d ? d->c_symbol : "?";
}

const char*
cdsl_op_name(cdsl_op_t op)
{
	const cdsl_op_desc_t* d = cdsl_op_get_desc(op);
	return d ? d->name : "?";
}
```

- [ ] **Step 4: 编译并运行单测验证**

运行：`cmake --build build --target test_ast && ctest --test-dir build -R test_ast --output-on-failure`
预期输出：构建成功，零编译器警告，`test_ast` 测试全部通过。

- [ ] **Step 5: 提交 Task 1 代码**

执行：
```bash
git add include/cdsl/ast.h src/ast/ast.c tests/test_ast.c
git commit -m "refactor(ast): ♻️ unify operator metadata descriptors and static lookup table"
```

---

### Task 2: 元数据项纯值语义与追加助手实现

**Files:**
- Modify: `include/cdsl/ast.h`
- Modify: `src/ast/ast.c`
- Test: `tests/test_ast.c`

- [ ] **Step 1: 在 `tests/test_ast.c` 中编写失败测试**

在 `tests/test_ast.c` 中新增专门的元数据值语义与助手测试函数 `test_meta_value_semantics()`：
```c
static void
test_meta_value_semantics(void)
{
	TEST_BEGIN("meta value semantics and append helper");

	/* Test 1: string literal safety with cdsl_create_meta_item and cdsl_free_meta */
	cdsl_meta_item_t* item = cdsl_create_meta_item("version", "1.0.0");
	TEST_ASSERT(item != NULL, "item created from literals");
	TEST_ASSERT_STR(item->key, "version", "item key matches");
	TEST_ASSERT_STR(item->value, "1.0.0", "item value matches");
	TEST_ASSERT(item->next == NULL, "item next is NULL");
	cdsl_free_meta(item);

	/* Test 2: NULL safety */
	TEST_ASSERT(cdsl_create_meta_item(NULL, "val") == NULL, "NULL key returns NULL");
	TEST_ASSERT(cdsl_create_meta_item("key", NULL) == NULL, "NULL value returns NULL");
	TEST_ASSERT(cdsl_create_meta_item(NULL, NULL) == NULL, "both NULL returns NULL");

	/* Test 3: cdsl_append_meta_kv convenience helper */
	cdsl_meta_item_t* list = NULL;
	list = cdsl_append_meta_kv(list, "k1", "v1");
	TEST_ASSERT(list != NULL, "list created by helper");
	TEST_ASSERT_STR(list->key, "k1", "first item key");
	TEST_ASSERT_STR(list->value, "v1", "first item value");

	list = cdsl_append_meta_kv(list, "k2", "v2");
	TEST_ASSERT(list->next != NULL, "second item appended");
	TEST_ASSERT_STR(list->next->key, "k2", "second item key");
	TEST_ASSERT_STR(list->next->value, "v2", "second item value");

	/* Appending with invalid params does not break list */
	list = cdsl_append_meta_kv(list, NULL, "v3");
	TEST_ASSERT(list->next->next == NULL, "invalid append preserves list");

	cdsl_free_meta(list);

	TEST_END();
}
```
并在 `main()` 中调用 `test_meta_value_semantics();`。

- [ ] **Step 2: 编译测试以确认失败**

运行：`cmake --build build --target test_ast`
预期输出：编译失败，提示 `cdsl_append_meta_kv` 未定义，或 `const char*` 传参类型警告。

- [ ] **Step 3: 在 `include/cdsl/ast.h` 和 `src/ast/ast.c` 中更新函数签名与实现**

在 `include/cdsl/ast.h` 中修改：
```c
/**
 * @brief 创建元数据项（内部始终执行深拷贝，调用方保留入参所有权）
 * @param key 键名字符串（支持字面量或堆内存，允许只读）
 * @param value 键值字符串（支持字面量或堆内存，允许只读）
 * @return 新分配的元数据项指针，入参非法或 OOM 时返回 NULL
 */
cdsl_meta_item_t* cdsl_create_meta_item(const char* key, const char* value);

/**
 * @brief 创建元数据项并直接追加至列表末尾的便捷助手函数
 * @param list 现有元数据链表头指针（允许为 NULL）
 * @param key 键名
 * @param value 键值
 * @return 更新后的元数据链表头指针
 */
cdsl_meta_item_t* cdsl_append_meta_kv(cdsl_meta_item_t* list, const char* key, const char* value);
```

在 `src/ast/ast.c` 中更新 `cdsl_create_meta_item` 并实现 `cdsl_append_meta_kv`：
```c
cdsl_meta_item_t*
cdsl_create_meta_item(const char* key, const char* value)
{
	if (!key || !value) {
		return NULL;
	}
	cdsl_meta_item_t* m = ast_alloc(sizeof(*m));
	if (!m) {
		return NULL;
	}
	if (current_ast_arena) {
		m->key = cdsl_arena_strdup(current_ast_arena, key);
		m->value = cdsl_arena_strdup(current_ast_arena, value);
	} else {
		m->key = strdup(key);
		m->value = strdup(value);
	}
	if (!m->key || !m->value) {
		if (!current_ast_arena) {
			free(m->key);
			free(m->value);
			free(m);
		}
		return NULL;
	}
	m->next = NULL;
	return m;
}

cdsl_meta_item_t*
cdsl_append_meta_kv(cdsl_meta_item_t* list, const char* key, const char* value)
{
	cdsl_meta_item_t* item = cdsl_create_meta_item(key, value);
	if (!item) {
		return list;
	}
	return cdsl_append_meta(list, item);
}
```

- [ ] **Step 4: 编译并运行单测验证**

运行：`cmake --build build --target test_ast && ctest --test-dir build -R test_ast --output-on-failure`
预期输出：构建成功，零警告，`test_ast` 全部通过，无 invalid pointer 或 leak。

- [ ] **Step 5: 提交 Task 2 代码**

执行：
```bash
git add include/cdsl/ast.h src/ast/ast.c tests/test_ast.c
git commit -m "refactor(ast): ♻️ implement deep-copy value semantics for meta items and append helper"
```

---

### Task 3: 迁移调用方（Bison 语法动作、模板复制与测试清理）

**Files:**
- Modify: `parser/parser.y`
- Modify: `src/ast/template.c`
- Modify: `tests/test_ast.c`
- Modify: `tests/test_execution.c`

- [ ] **Step 1: 迁移 `parser/parser.y`**

修改 `template_declaration` 产生式，改用 `cdsl_append_meta_kv`：
```yacc
template_declaration:
    TEMPLATE IDENTIFIER LBRACE meta_block metric_list RBRACE {
        $$ = cdsl_create_metric_rule($2, $4, $5, @1.first_line, @1.first_column);
        if ($$) {
            $$->meta_list = cdsl_append_meta_kv($$->meta_list, "__is_template", "true");
        }
    }
    ;
```

修改 `meta_item` 产生式，在语法动作中显式释放 lexer 分配的 `$1` 和 `$3`：
```yacc
meta_item:
    IDENTIFIER ASSIGN STRING_LIT {
        $$ = cdsl_create_meta_item($1, $3);
        free($1);
        free($3);
    }
    | error                                  { $$ = NULL; }
    ;
```

- [ ] **Step 2: 迁移 `src/ast/template.c`**

在 `copy_metric_list` 中，使用 `cdsl_append_meta_kv` 简化元数据复制逻辑：
```c
		cdsl_meta_item_t* meta = NULL;
		for (cdsl_meta_item_t* mi = m->meta_list; mi; mi = mi->next) {
			meta = cdsl_append_meta_kv(meta, mi->key, mi->value);
		}
```
完全移除对 `strdup(mi->key)`、`strdup(mi->value)` 的手工调用和错误清理逻辑。

- [ ] **Step 3: 清理现有测试中的手工 `strdup`**

在 `tests/test_ast.c` 中：
- 将现有 `cdsl_create_meta_item(strdup("key1"), strdup("val1"))` 简化为 `cdsl_create_meta_item("key1", "val1")`。
- 将现有 `cdsl_create_meta_item(strdup("key2"), strdup("val2"))` 简化为 `cdsl_create_meta_item("key2", "val2")`。
- 将现有 `cdsl_create_meta_item(strdup("description"), strdup("test rule"))` 简化为 `cdsl_create_meta_item("description", "test rule")`。

在 `tests/test_execution.c` 中：
- 将现有 `cdsl_create_meta_item(strdup("weight"), strdup("85"))` 简化为 `cdsl_create_meta_item("weight", "85")`。
- 将后续 `cdsl_append_meta(meta, cdsl_create_meta_item(strdup("is_critical"), strdup("true")))` 简化为 `cdsl_append_meta_kv(meta, "is_critical", "true")`。
- 将后续 `cdsl_append_meta(meta, cdsl_create_meta_item(strdup("bad_int"), strdup("xyz")))` 简化为 `cdsl_append_meta_kv(meta, "bad_int", "xyz")`。

- [ ] **Step 4: 编译并运行相关测试**

运行：
```bash
cmake --build build --target test_ast test_template test_execution test_parallel_parse
ctest --test-dir build -R "test_ast|test_template|test_execution|test_parallel_parse" --output-on-failure
```
预期输出：全部构建成功，所有相关测试通过。

- [ ] **Step 5: 提交 Task 3 代码**

执行：
```bash
git add parser/parser.y src/ast/template.c tests/test_ast.c tests/test_execution.c
git commit -m "refactor: ♻️ migrate meta item callers to value semantics and cleanup redundant strdup"
```

---

### Task 4: 全量回归与内存泄漏验证

**Files:**
- 全局测试回归

- [ ] **Step 1: 全量构建并执行 CTest**

运行：
```bash
cmake --build build
ctest --test-dir build --output-on-failure
```
预期输出：28/28 测试全部通过，无警告、无错误。

- [ ] **Step 2: 内存安全验证 (Valgrind / AddressSanitizer)**

运行：
```bash
valgrind --leak-check=full --error-exitcode=1 ./build/test_ast
valgrind --leak-check=full --error-exitcode=1 ./build/test_template
valgrind --leak-check=full --error-exitcode=1 ./build/test_execution
```
预期输出：所有被测二进制均正常退出（exitcode 0），零内存泄漏与非法指针释放错误。

- [ ] **Step 3: 合并工作分支并清理 Worktree**

将分支 `refactor/unified-op-desc-and-meta` 合并回 `master`，清理 worktree：
```bash
git checkout master
git merge --no-ff refactor/unified-op-desc-and-meta -m "merge: 🔀 merge refactor/unified-op-desc-and-meta into master"
git worktree remove .worktrees/unified-op-desc-and-meta
git branch -d refactor/unified-op-desc-and-meta
```
再次在 `master` 运行 `ctest --test-dir build --output-on-failure` 确认全绿。
