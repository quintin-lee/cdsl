# 工具函数去重与显式枚举契约治理实施计划 (Implementation Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 消除 VM 与各模块中重复的工具函数（如 `safe_atoi` 与元数据解析），并在 AST/VM 边界建立完备且类型安全的显式枚举契约与谓词映射，保障零警告编译与长期可维护性。

**Architecture:** 在 `include/cdsl/ast.h` 与 `src/ast/ast.c` 增加操作符分类谓词与显式名称/符号映射；在 `src/vm/internal.h` 与 `src/vm/eval.c` 统一防御性整型解析与规则/指标元数据提取；各消费模块（`codegen`, `visualize`, `schema`, `bytecode`, `eval`, `bridge`, `error`）全面迁移至统一接口，消除裸数组索引与魔数范围判断。

**Tech Stack:** C99/C23, CMake, CTest, GCC/Clang (-Wall -Wextra -Werror)

---

## 涉及文件与职责规划

| 文件路径 | 动作 | 职责描述 |
|---|---|---|
| `include/cdsl/ast.h` | 修改 | 声明算子谓词（关系/算术/逻辑/一元）、C符号映射、DOT名称映射、类型名称映射 |
| `src/ast/ast.c` | 修改 | 实现全覆盖 switch 映射与分类谓词函数，杜绝隐式数组下标越界 |
| `src/vm/internal.h` | 修改 | 声明 `cdsl_safe_atoi`、`cdsl_meta_get_int`、`cdsl_meta_get_bool` |
| `src/vm/eval.c` | 修改 | 实现统一的 `cdsl_safe_atoi` 与元数据提取工具，移除本地冗余 `safe_atoi` 与 `get_metric_meta` |
| `src/util/error.c` | 修改 | 在 `kind_str` 中补全 `CDSL_ERR_WARNING` 映射为 `"WARNING"` |
| `src/ai/bridge.c` | 修改 | 移除局部 `type_to_str`，改用公共的 `cdsl_type_name` |
| `src/schema/schema.c` | 修改 | 用 `cdsl_op_is_logical_binary` 与 `cdsl_op_is_arithmetic` 替换裸区间判断 |
| `src/vm/codegen.c` | 修改 | 移除局部 `safe_atoi`，用 `cdsl_op_c_symbol` 替换 `ops[op_idx]`，使用元数据提取工具 |
| `src/vm/visualize.c` | 修改 | 移除局部 `safe_atoi`，用 `cdsl_op_name` 替换 `ops[op_idx]`，使用元数据提取工具 |
| `src/vm/bytecode.c` | 修改 | 移除局部 `bc_safe_atoi`，使用算子谓词与元数据提取工具 |
| `tests/test_ast.c` | 修改 | 增加算子谓词分类、符号映射与类型名称单元测试 |
| `tests/test_execution.c` | 修改 | 增加 `cdsl_safe_atoi` 边界与元数据提取测试用例 |
| `tests/test_error.c` | 修改 | 增加 `CDSL_ERR_WARNING` 格式化验证用例 |

---

### Task 1: AST 层枚举谓词与映射函数实现与测试

**Files:**
- Modify: `include/cdsl/ast.h`
- Modify: `src/ast/ast.c`
- Test: `tests/test_ast.c`

- [ ] **Step 1: 在 `tests/test_ast.c` 中编写失败测试**

在 `tests/test_ast.c` 的 `main()` 前追加测试函数 `test_ast_operator_and_type_contracts()`：

```c
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

	TEST_END();
}
```
并在 `main()` 中调用 `test_ast_operator_and_type_contracts();`。

- [ ] **Step 2: 运行测试以确认编译失败**

运行：`cmake --build build --target test_ast`
预期输出：编译失败，提示 `cdsl_op_is_relational` 未定义。

- [ ] **Step 3: 在 `include/cdsl/ast.h` 和 `src/ast/ast.c` 中实现这些函数**

在 `include/cdsl/ast.h` 中追加函数声明：
```c
bool cdsl_op_is_relational(cdsl_op_t op);
bool cdsl_op_is_arithmetic(cdsl_op_t op);
bool cdsl_op_is_logical_binary(cdsl_op_t op);
bool cdsl_op_is_unary(cdsl_op_t op);
const char* cdsl_op_c_symbol(cdsl_op_t op);
const char* cdsl_op_name(cdsl_op_t op);
const char* cdsl_type_name(cdsl_type_t type);
```

在 `src/ast/ast.c` 中追加实现：
```c
bool
cdsl_op_is_relational(cdsl_op_t op)
{
	switch (op) {
	case CDSL_OP_EQ:
	case CDSL_OP_NE:
	case CDSL_OP_LT:
	case CDSL_OP_GT:
	case CDSL_OP_LE:
	case CDSL_OP_GE:
		return true;
	case CDSL_OP_AND:
	case CDSL_OP_OR:
	case CDSL_OP_NOT:
	case CDSL_OP_ADD:
	case CDSL_OP_SUB:
	case CDSL_OP_MUL:
	case CDSL_OP_DIV:
	case CDSL_OP_NEG:
	default:
		return false;
	}
}

bool
cdsl_op_is_arithmetic(cdsl_op_t op)
{
	switch (op) {
	case CDSL_OP_ADD:
	case CDSL_OP_SUB:
	case CDSL_OP_MUL:
	case CDSL_OP_DIV:
		return true;
	case CDSL_OP_EQ:
	case CDSL_OP_NE:
	case CDSL_OP_LT:
	case CDSL_OP_GT:
	case CDSL_OP_LE:
	case CDSL_OP_GE:
	case CDSL_OP_AND:
	case CDSL_OP_OR:
	case CDSL_OP_NOT:
	case CDSL_OP_NEG:
	default:
		return false;
	}
}

bool
cdsl_op_is_logical_binary(cdsl_op_t op)
{
	switch (op) {
	case CDSL_OP_AND:
	case CDSL_OP_OR:
		return true;
	case CDSL_OP_EQ:
	case CDSL_OP_NE:
	case CDSL_OP_LT:
	case CDSL_OP_GT:
	case CDSL_OP_LE:
	case CDSL_OP_GE:
	case CDSL_OP_NOT:
	case CDSL_OP_ADD:
	case CDSL_OP_SUB:
	case CDSL_OP_MUL:
	case CDSL_OP_DIV:
	case CDSL_OP_NEG:
	default:
		return false;
	}
}

bool
cdsl_op_is_unary(cdsl_op_t op)
{
	switch (op) {
	case CDSL_OP_NOT:
	case CDSL_OP_NEG:
		return true;
	case CDSL_OP_EQ:
	case CDSL_OP_NE:
	case CDSL_OP_LT:
	case CDSL_OP_GT:
	case CDSL_OP_LE:
	case CDSL_OP_GE:
	case CDSL_OP_AND:
	case CDSL_OP_OR:
	case CDSL_OP_ADD:
	case CDSL_OP_SUB:
	case CDSL_OP_MUL:
	case CDSL_OP_DIV:
	default:
		return false;
	}
}

const char*
cdsl_op_c_symbol(cdsl_op_t op)
{
	switch (op) {
	case CDSL_OP_EQ:
		return "==";
	case CDSL_OP_NE:
		return "!=";
	case CDSL_OP_LT:
		return "<";
	case CDSL_OP_GT:
		return ">";
	case CDSL_OP_LE:
		return "<=";
	case CDSL_OP_GE:
		return ">=";
	case CDSL_OP_AND:
		return "&&";
	case CDSL_OP_OR:
		return "||";
	case CDSL_OP_NOT:
		return "!";
	case CDSL_OP_ADD:
		return "+";
	case CDSL_OP_SUB:
		return "-";
	case CDSL_OP_MUL:
		return "*";
	case CDSL_OP_DIV:
		return "/";
	case CDSL_OP_NEG:
		return "-";
	default:
		return "?";
	}
}

const char*
cdsl_op_name(cdsl_op_t op)
{
	switch (op) {
	case CDSL_OP_EQ:
		return "==";
	case CDSL_OP_NE:
		return "!=";
	case CDSL_OP_LT:
		return "<";
	case CDSL_OP_GT:
		return ">";
	case CDSL_OP_LE:
		return "<=";
	case CDSL_OP_GE:
		return ">=";
	case CDSL_OP_AND:
		return "AND";
	case CDSL_OP_OR:
		return "OR";
	case CDSL_OP_NOT:
		return "NOT";
	case CDSL_OP_ADD:
		return "+";
	case CDSL_OP_SUB:
		return "-";
	case CDSL_OP_MUL:
		return "*";
	case CDSL_OP_DIV:
		return "/";
	case CDSL_OP_NEG:
		return "NEG";
	default:
		return "?";
	}
}

const char*
cdsl_type_name(cdsl_type_t type)
{
	switch (type) {
	case CDSL_TYPE_INT:
		return "INT";
	case CDSL_TYPE_FLOAT:
		return "FLOAT";
	case CDSL_TYPE_BOOL:
		return "BOOL";
	case CDSL_TYPE_STRING:
		return "STRING";
	case CDSL_TYPE_DATE:
		return "DATE";
	case CDSL_TYPE_LONG:
		return "LONG";
	case CDSL_TYPE_ARRAY:
		return "ARRAY";
	case CDSL_TYPE_VOID:
		return "VOID";
	default:
		return "UNKNOWN";
	}
}
```

- [ ] **Step 4: 重新编译并执行测试**

运行：`cmake --build build --target test_ast && ctest --test-dir build -R test_ast --output-on-failure`
预期输出：`test_ast ... Passed`。

- [ ] **Step 5: 提交更改**

```bash
git add include/cdsl/ast.h src/ast/ast.c tests/test_ast.c
git commit -m "feat(ast): ✨ add operator predicates, symbol mappers, and type name functions"
```

---

### Task 2: VM 内部通用工具与元数据解析函数实现与测试

**Files:**
- Modify: `src/vm/internal.h`
- Modify: `src/vm/eval.c`
- Test: `tests/test_execution.c`

- [ ] **Step 1: 在 `tests/test_execution.c` 中添加对 `cdsl_safe_atoi` 与元数据提取的测试**

在 `tests/test_execution.c` 中包含 `"../src/vm/internal.h"`，添加测试函数并在 `main()` 中调用：

```c
static void
test_vm_internal_utils(void)
{
	TEST_BEGIN("vm internal utils: safe_atoi and meta extraction");

	/* safe_atoi boundary tests */
	TEST_ASSERT_INT(cdsl_safe_atoi("123", 0), 123, "safe_atoi normal positive");
	TEST_ASSERT_INT(cdsl_safe_atoi("-456", 0), -456, "safe_atoi normal negative");
	TEST_ASSERT_INT(cdsl_safe_atoi(NULL, 99), 99, "safe_atoi NULL returns default");
	TEST_ASSERT_INT(cdsl_safe_atoi("", 99), 99, "safe_atoi empty returns default");
	TEST_ASSERT_INT(cdsl_safe_atoi("abc", 99), 99, "safe_atoi alpha returns default");
	TEST_ASSERT_INT(cdsl_safe_atoi("123abc", 99), 99, "safe_atoi trailing garbage returns default");
	TEST_ASSERT_INT(cdsl_safe_atoi("99999999999999999999999999", 99), 99, "safe_atoi overflow returns default");

	/* meta extraction tests */
	cdsl_meta_item_t* meta = cdsl_create_meta_item("weight", "85");
	meta = cdsl_append_meta(meta, cdsl_create_meta_item("is_critical", "true"));
	meta = cdsl_append_meta(meta, cdsl_create_meta_item("bad_int", "xyz"));

	TEST_ASSERT_INT(cdsl_meta_get_int(meta, "weight", 0), 85, "meta get int weight");
	TEST_ASSERT_INT(cdsl_meta_get_int(meta, "missing_key", 10), 10, "meta get int missing default");
	TEST_ASSERT_INT(cdsl_meta_get_int(meta, "bad_int", 20), 20, "meta get int invalid default");
	TEST_ASSERT_INT(cdsl_meta_get_int(NULL, "weight", 30), 30, "meta get int NULL list default");

	TEST_ASSERT_INT(cdsl_meta_get_bool(meta, "is_critical", 0), 1, "meta get bool true");
	TEST_ASSERT_INT(cdsl_meta_get_bool(meta, "missing_bool", 0), 0, "meta get bool missing default");
	TEST_ASSERT_INT(cdsl_meta_get_bool(meta, "bad_int", 0), 0, "meta get bool invalid default");
	TEST_ASSERT_INT(cdsl_meta_get_bool(NULL, "is_critical", 1), 1, "meta get bool NULL default");

	cdsl_free_meta(meta);

	TEST_END();
}
```

- [ ] **Step 2: 运行编译以验证测试失败**

运行：`cmake --build build --target test_execution`
预期输出：报错提示 `cdsl_safe_atoi` 未声明。

- [ ] **Step 3: 在 `src/vm/internal.h` 和 `src/vm/eval.c` 中实现**

在 `src/vm/internal.h` 中追加声明：
```c
int cdsl_safe_atoi(const char* str, int default_val);
int cdsl_meta_get_int(cdsl_meta_item_t* list, const char* key, int default_val);
int cdsl_meta_get_bool(cdsl_meta_item_t* list, const char* key, int default_val);
```

在 `src/vm/eval.c` 中，将原 `static int safe_atoi` 重命名为 `cdsl_safe_atoi` 并作为非 static 函数暴露；并实现 `cdsl_meta_get_int` 与 `cdsl_meta_get_bool`：
```c
int
cdsl_safe_atoi(const char* str, int default_val)
{
	if (!str) {
		return default_val;
	}
	char* end = NULL;
	errno = 0;
	long val = strtol(str, &end, 10);
	if (errno != 0 || end == str || *end != '\0') {
		return default_val;
	}
	return (int)val;
}

int
cdsl_meta_get_int(cdsl_meta_item_t* list, const char* key, int default_val)
{
	char* val = cdsl_meta_get(list, key);
	return cdsl_safe_atoi(val, default_val);
}

int
cdsl_meta_get_bool(cdsl_meta_item_t* list, const char* key, int default_val)
{
	char* val = cdsl_meta_get(list, key);
	if (!val) {
		return default_val;
	}
	if (strcmp(val, "true") == 0) {
		return 1;
	}
	if (strcmp(val, "false") == 0) {
		return 0;
	}
	return default_val;
}
```

- [ ] **Step 4: 编译并运行测试**

运行：`cmake --build build --target test_execution && ctest --test-dir build -R test_execution --output-on-failure`
预期输出：`test_execution ... Passed`。

- [ ] **Step 5: 提交更改**

```bash
git add src/vm/internal.h src/vm/eval.c tests/test_execution.c
git commit -m "feat(vm): ✨ implement centralized cdsl_safe_atoi and meta extraction utilities"
```

---

### Task 3: 补全 `error.c` 与 `bridge.c` 的枚举映射缺失项与测试

**Files:**
- Modify: `src/util/error.c`
- Modify: `src/ai/bridge.c`
- Test: `tests/test_error.c`

- [ ] **Step 1: 在 `tests/test_error.c` 中增加针对 `CDSL_ERR_WARNING` 的格式化测试**

在 `tests/test_error.c` 中添加：
```c
static void
test_error_warning_kind_formatting(void)
{
	TEST_ERROR_BEGIN("error warning kind formatting");
	cdsl_error_t* err = cdsl_error_create(CDSL_ERR_WARNING, 10, 5, "Unused rule", "Remove it");
	TEST_ASSERT_NOT_NULL(err, "error created");
	char* str = cdsl_error_format(err);
	TEST_ASSERT_NOT_NULL(str, "formatted string not null");
	TEST_ASSERT(strstr(str, "WARNING") != NULL, "formatted string contains WARNING");
	TEST_ASSERT(strstr(str, "UNKNOWN") == NULL, "formatted string does not contain UNKNOWN");
	free(str);
	cdsl_error_free(err);
	TEST_ERROR_END();
}
```
并在 `main()` 中调用 `test_error_warning_kind_formatting();`。

- [ ] **Step 2: 运行测试以确认当前测试失败**

运行：`cmake --build build --target test_error && ctest --test-dir build -R test_error --output-on-failure`
预期输出：失败，因为目前 `kind_str` 返回 `"UNKNOWN"`。

- [ ] **Step 3: 修复 `src/util/error.c` 与 `src/ai/bridge.c`**

在 `src/util/error.c` 中的 `kind_str` 添加：
```c
	case CDSL_ERR_WARNING:
		return "WARNING";
```

在 `src/ai/bridge.c` 中，移除私有的 `type_to_str(cdsl_type_t t)`，替换所有对 `type_to_str(...)` 的调用为 `cdsl_type_name(...)`。

- [ ] **Step 4: 编译并运行测试**

运行：`cmake --build build && ctest --test-dir build -R "test_error|test_ai_extensions" --output-on-failure`
预期输出：全部 Passed。

- [ ] **Step 5: 提交更改**

```bash
git add src/util/error.c src/ai/bridge.c tests/test_error.c
git commit -m "fix(util,ai): 🐛 support CDSL_ERR_WARNING in error formatter and use cdsl_type_name in bridge"
```

---

### Task 4: 重构 `schema.c`，使用 AST 谓词替换裸魔数区间比较

**Files:**
- Modify: `src/schema/schema.c`
- Test: `tests/test_schema.c`, `tests/test_validation_errors.c`

- [ ] **Step 1: 验证当前测试正常**

运行：`ctest --test-dir build -R "test_schema|test_validation_errors" --output-on-failure`
预期输出：100% Passed。

- [ ] **Step 2: 修改 `src/schema/schema.c`**

将：
```c
if (op >= CDSL_OP_AND && op <= CDSL_OP_OR) {
```
替换为：
```c
if (cdsl_op_is_logical_binary(op)) {
```

将：
```c
if (op >= CDSL_OP_ADD && op <= CDSL_OP_DIV) {
```
替换为：
```c
if (cdsl_op_is_arithmetic(op)) {
```

- [ ] **Step 3: 重新编译并运行测试**

运行：`cmake --build build && ctest --test-dir build -R "test_schema|test_validation_errors" --output-on-failure`
预期输出：编译无警告，测试全部 Passed。

- [ ] **Step 4: 提交更改**

```bash
git add src/schema/schema.c
git commit -m "refactor(schema): ♻️ replace raw operator range checks with AST semantic predicates"
```

---

### Task 5: 重构 `codegen.c` 与 `visualize.c`，消除局部 `safe_atoi` 并用显式映射替换 `ops[]` 数组

**Files:**
- Modify: `src/vm/codegen.c`
- Modify: `src/vm/visualize.c`
- Test: `tests/test_codegen.c`, `tests/test_visual.c`

- [ ] **Step 1: 重构 `src/vm/codegen.c`**

1. 移除文件开头的 `static int safe_atoi(...)`。
2. 包含 `"internal.h"`（若未包含）。
3. 替换 `codegen_expr` 中的 `CDSL_EXPR_BINARY` 处理：
   ```c
   /* 替换原有 ops 数组与下标越界判断 */
   const char* op_str = cdsl_op_c_symbol(op);
   cdsl_strbuf_printf(sb, " %s ", op_str);
   ```
4. 替换元数据解析：
   ```c
   int pass_thresh = cdsl_meta_get_int(rule->meta_list, "pass_threshold", 80);
   int partial_thresh = cdsl_meta_get_int(rule->meta_list, "partial_threshold", 60);
   int weight = cdsl_meta_get_int(m->meta_list, "weight", 0);
   int critical = cdsl_meta_get_bool(m->meta_list, "is_critical", 0);
   ```

- [ ] **Step 2: 重构 `src/vm/visualize.c`**

1. 移除文件开头的 `static int safe_atoi(...)`。
2. 包含 `"internal.h"`。
3. 替换 `dot_expr` 中 `CDSL_EXPR_BINARY` 的 `ops` 数组索引为：
   ```c
   const char* op_str = cdsl_op_name(expr->data.binary.op);
   cdsl_strbuf_printf(sb, "  n%d [label=\"%s\",shape=diamond,style=filled,fillcolor=lightgreen];\n", my_id, op_str);
   ```
4. 替换元数据解析：
   ```c
   int weight = cdsl_meta_get_int(m->meta_list, "weight", 0);
   int critical = cdsl_meta_get_bool(m->meta_list, "is_critical", 0);
   ```

- [ ] **Step 3: 编译并运行测试**

运行：`cmake --build build && ctest --test-dir build -R "test_codegen|test_visual" --output-on-failure`
预期输出：编译 0 警告，测试全部 Passed。

- [ ] **Step 4: 提交更改**

```bash
git add src/vm/codegen.c src/vm/visualize.c
git commit -m "refactor(vm): ♻️ dedup safe_atoi and replace raw ops array indexing in codegen and visualize"
```

---

### Task 6: 重构 `bytecode.c` 与 `eval.c`，收敛工具函数并使用谓词

**Files:**
- Modify: `src/vm/bytecode.c`
- Modify: `src/vm/eval.c`
- Test: `tests/test_bytecode.c`, `tests/test_execution.c`, `tests/test_ruleset.c`

- [ ] **Step 1: 重构 `src/vm/bytecode.c`**

1. 移除 `static int bc_safe_atoi(...)`。
2. 将 `bc_safe_atoi` 调用点替换为 `cdsl_meta_get_int(...)` 与 `cdsl_meta_get_bool(...)`：
   ```c
   bc->pass_threshold = cdsl_meta_get_int(rule->meta_list, "pass_threshold", 80);
   bc->partial_threshold = cdsl_meta_get_int(rule->meta_list, "partial_threshold", 60);
   bc->is_metric_rule = (rule->metrics != NULL);
   ...
   int m_weight = cdsl_meta_get_int(m->meta_list, "weight", 0);
   bc->metric_infos[midx].is_critical = cdsl_meta_get_bool(m->meta_list, "is_critical", 0);
   ```
3. 在常量折叠逻辑 `compile_expr` 中：
   将 `if (op >= CDSL_OP_ADD && op <= CDSL_OP_DIV)` 替换为 `if (cdsl_op_is_arithmetic(op))`。
   将 `if (op >= CDSL_OP_EQ && op <= CDSL_OP_GE)` 替换为 `if (cdsl_op_is_relational(op))`。

- [ ] **Step 2: 重构 `src/vm/eval.c`**

1. 移除 `get_metric_meta`。
2. 将原本地解析替换为 `cdsl_meta_get_int` 与 `cdsl_meta_get_bool`：
   ```c
   mr->max_weight = cdsl_meta_get_int(m->meta_list, "weight", 0);
   mr->is_critical = cdsl_meta_get_bool(m->meta_list, "is_critical", 0);
   ...
   int pass_thresh = cdsl_meta_get_int(rule->meta_list, "pass_threshold", 80);
   int partial_thresh = cdsl_meta_get_int(rule->meta_list, "partial_threshold", 60);
   ```

- [ ] **Step 3: 运行全部 VM 测试**

运行：`cmake --build build && ctest --test-dir build -R "test_bytecode|test_execution|test_ruleset|test_runtime_errors" --output-on-failure`
预期输出：全部 Passed。

- [ ] **Step 4: 提交更改**

```bash
git add src/vm/bytecode.c src/vm/eval.c
git commit -m "refactor(vm): ♻️ migrate bytecode and eval to shared meta utils and AST operator predicates"
```

---

### Task 7: 完整构建验证与全量回归测试

**Files:**
- 全局验证

- [ ] **Step 1: 重新完整编译（清理后全量编译）以验证 0 警告**

运行：`cmake --build build --clean-first`
预期输出：成功编译全部 27 个测试及静态/动态库目标，无任何警告与报错。

- [ ] **Step 2: 运行全量 27 个测试套件**

运行：`ctest --test-dir build --output-on-failure`
预期输出：`100% tests passed out of 27`。

- [ ] **Step 3: 验证无残留重复函数**

运行：
```bash
grep -rn "safe_atoi" src/
```
预期输出：仅在 `src/vm/internal.h` 和 `src/vm/eval.c` 中出现 `cdsl_safe_atoi`。

- [ ] **Step 4: 验证无裸 `ops[]` 数组**

运行：
```bash
grep -rn 'ops\[' src/
```
预期输出：无任何结果。
