# 设计文档：统一算子元数据描述与元数据项值语义重构

**日期**: 2026-11-07
**项目**: C-DSL (cdsl)
**作者**: Antigravity & User Pair Programming
**状态**: 待审核 (Ready for Review)

---

## 1. 背景与问题分析

在完成算子谓词化与工具函数去重后，代码库在健壮性和安全性上取得了长足进步。但随着对底层细节的进一步审视，仍存在以下两处可深入优化的设计问题：

### 1.1 算子属性的分散多处维护
当前在 [`include/cdsl/ast.h`](file:///home/quintin/Data/source/c_cpp/cdsl/include/cdsl/ast.h) 和 [`src/ast/ast.c`](file:///home/quintin/Data/source/c_cpp/cdsl/src/ast/ast.c) 中，针对 14 个 `cdsl_op_t` 枚举项分别维护了 6 个独立的 `switch-case` 函数：
- `cdsl_op_is_relational(op)`
- `cdsl_op_is_arithmetic(op)`
- `cdsl_op_is_logical_binary(op)`
- `cdsl_op_is_unary(op)`
- `cdsl_op_c_symbol(op)`
- `cdsl_op_name(op)`

这些函数各自展开 14 个 case 分支。一旦未来引入新的运算符（如取模 `%`、位运算 `&`, `|`、三元条件运算 `? :` 等），开发者必须同步在 6 处分别添加分支。缺乏单一真实数据源（Single Source of Truth），容易导致遗漏和维护不同步。

### 1.2 `meta->key` 与 `meta->value` 的所有权混淆
[`include/cdsl/ast.h`](file:///home/quintin/Data/source/c_cpp/cdsl/include/cdsl/ast.h) 中的 `cdsl_create_meta_item` 签名如下：
```c
cdsl_meta_item_t* cdsl_create_meta_item(char* key, char* value);
```
其内部实现存在隐式的所有权转移假设：
- 当处于 arena 模式时，它先复制再调用 `free(key)` 与 `free(value)`；
- 当处于普通堆模式时，它直接接管 `key` 与 `value` 的指针，而在析构函数 `cdsl_free_meta` 中无条件执行 `free(meta->key)` 和 `free(meta->value)`。

**危害**：
1. **字面量崩溃陷阱**：调用方若以纯字符串字面量传参（如 `cdsl_create_meta_item("weight", "85")`），在后续释放时会触发 `free()` 非法地址段错误或未定义行为。
2. **测试与业务样板代码繁重**：所有外部调用者和单元测试被迫在传参前手工调用 `strdup`（如 `cdsl_create_meta_item(strdup("weight"), strdup("85"))`），样板代码臃肿且极易疏忽。
3. **复制逻辑冗余**：在模板实例化指标列表（`copy_metric_list`）时，需要手工为每个 key/value 分配临时副本再由内部接管，代码冗余脆弱。

---

## 2. 目标与设计原则

1. **单一数据源 (Single Source of Truth)**：通过集中式算子描述符表 `cdsl_op_desc_t` 一站式描述算子全部元数据，编译期静态断言强制校验表项完备性，现有 6 个函数全面收敛为单行委托。
2. **纯粹值语义 (Value Semantics)**：将 `cdsl_create_meta_item` 重构为接收 `const char*` 入参，内部始终通过 `strdup` / `arena_strdup` 维护独立生命周期。
3. **零所有权混淆**：调用方永远保留原输入字符串所有权，传入字面量绝对安全，外部再无须手工 `strdup`。
4. **编译与回归安全**：保持 `-Wall -Wextra -Werror` 零警告，全库 28 项测试套件 100% 通过。

---

## 3. 详细设计

### 3.1 算子元数据集中描述符与静态表

#### 3.1.1 接口声明 (`include/cdsl/ast.h`)

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

#### 3.1.2 静态表与实现 (`src/ast/ast.c`)

在 `src/ast/ast.c` 中定义 `s_op_table` 数组，并通过 C11 `_Static_assert` 约束表长度与 `CDSL_OP_COUNT` 严格相等：

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
```

#### 3.1.3 现有查询函数的轻量委托化

原有 6 个函数内部各自数十行的 switch-case 彻底重构为 2 行的优雅委托：
```c
bool cdsl_op_is_relational(cdsl_op_t op) {
	const cdsl_op_desc_t* d = cdsl_op_get_desc(op);
	return d ? d->is_relational : false;
}

bool cdsl_op_is_arithmetic(cdsl_op_t op) {
	const cdsl_op_desc_t* d = cdsl_op_get_desc(op);
	return d ? d->is_arithmetic : false;
}

bool cdsl_op_is_logical_binary(cdsl_op_t op) {
	const cdsl_op_desc_t* d = cdsl_op_get_desc(op);
	return d ? (d->is_logical && d->arity == 2) : false;
}

bool cdsl_op_is_unary(cdsl_op_t op) {
	const cdsl_op_desc_t* d = cdsl_op_get_desc(op);
	return d ? (d->arity == 1) : false;
}

const char* cdsl_op_c_symbol(cdsl_op_t op) {
	const cdsl_op_desc_t* d = cdsl_op_get_desc(op);
	return d ? d->c_symbol : "?";
}

const char* cdsl_op_name(cdsl_op_t op) {
	const cdsl_op_desc_t* d = cdsl_op_get_desc(op);
	return d ? d->name : "?";
}
```

---

### 3.2 元数据项值语义与生命周期改造

#### 3.2.1 接口升级 (`include/cdsl/ast.h`)

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

#### 3.2.2 实现细节 (`src/ast/ast.c`)

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

---

### 3.3 各调用端迁移

1. **Bison 解析器 (`parser/parser.y`)**：
   - `template_declaration` 产生式：
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
   - `meta_item` 产生式：词法分析器产生的 `$1` 和 `$3` 在传给 `cdsl_create_meta_item` 后由语法动作显式 `free`：
     ```yacc
     meta_item:
         IDENTIFIER ASSIGN STRING_LIT {
             $$ = cdsl_create_meta_item($1, $3);
             free($1);
             free($3);
         }
         | error { $$ = NULL; }
         ;
     ```
2. **模板指标复制 (`src/ast/template.c`)**：
   - 消除手工分配和清理样板代码，单行调用：
     ```c
     for (cdsl_meta_item_t* mi = m->meta_list; mi; mi = mi->next) {
         meta = cdsl_append_meta_kv(meta, mi->key, mi->value);
     }
     ```
3. **测试用例清理 (`tests/test_ast.c` & `tests/test_execution.c`)**：
   - 移除测试中所有对字面量进行 `strdup` 的冗余调用，直接传 `"weight"`、`"85"` 等字符串常量。

---

## 4. 边界条件与防御性设计

| 场景 | 输入 | 预期行为 |
|---|---|---|
| **非法算子枚举查询** | `cdsl_op_get_desc((cdsl_op_t)999)` 或 `-1` | 安全返回 `NULL`，不越界 |
| **代理查询函数容错** | `cdsl_op_c_symbol((cdsl_op_t)999)` | 返回 `"?"`；谓词函数返回 `false` |
| **元数据空指针输入** | `cdsl_create_meta_item(NULL, "val")` | 立即返回 `NULL`，无堆分配 |
| **内部 OOM 容错** | `strdup` 失败 | 及时释放半成品并返回 `NULL`，无内存泄露 |
| **追加助手传参容错** | `cdsl_append_meta_kv(list, NULL, "v")` | 内部创建失败，安全返回原未受损的 `list` |

---

## 5. 验证与测试方案

1. **测试驱动用例扩充 (`tests/test_ast.c`)**：
   - 遍历 14 个枚举验证 `cdsl_op_get_desc` 各项属性；
   - 验证越界枚举返回 `NULL`；
   - 使用字符串字面量创建 `cdsl_create_meta_item`，调用 `cdsl_free_meta` 验证零段错误；
   - 测试 `cdsl_append_meta_kv` 链式调用。
2. **构建与全量回归**：
   - `-Wall -Wextra -Werror` 零警告；
   - `ctest --test-dir build --output-on-failure` 28 个测试套件 100% 通过。
