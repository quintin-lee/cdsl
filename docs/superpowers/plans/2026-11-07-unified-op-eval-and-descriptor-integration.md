# 算子描述符表与统一求值层深度整合实施计划 (Implementation Plan)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 将 AST 算子描述符表与统一求值层（`value_ops` + `bytecode` VM）深度打通，建立通用的三值关系比较计算器 `cdsl_op_eval_cmp`，消除散落在各类型的 8 处重复三元级联；并在 VM 内部建立紧凑的字节码双向转换契约，合并 VM 核心执行循环中的二元算子派发逻辑，净精简全库 ~150 行重复样板代码。

**Architecture:**
1. **通用关系比较契约**: 在 `include/cdsl/ast.h` 声明并实现 `static inline bool cdsl_op_eval_cmp(cdsl_op_t op, int cmp)`，提供零开销内联展开。
2. **统一求值层收敛**: 在 `src/vm/value_ops.c` 中移除私有谓词 `is_comparison_op` 与 `is_arithmetic_op`，改用 AST 公共谓词；将 STRING、DATE、BOOL、FLOAT、LONG、INT 的关系比较全部收敛为“提取三值标量 `cmp` + 委派 `cdsl_op_eval_cmp`”。
3. **VM 字节码双向映射与合并派发**: 在 `src/vm/internal.h` 与 `src/vm/bytecode.c` 实现 `cdsl_op_to_bc` 与 `cdsl_bc_to_op`；在编译发射中单行分派；在 VM 运行循环中合并 `BC_ADD..BC_DIV` 与 `BC_EQ..BC_GE`，消除 ~70 行重复代码。
4. **消费端治理**: 在 `src/vm/codegen.c` 移除手工 `cmp_op` 映射，在 `src/schema/schema.c` 中统一常量折叠比较。

**Tech Stack:** C99/C23, CMake, CTest, GCC/Clang (-Wall -Wextra -Werror)

**Worktree 规范:**
- 隔离目录: `.worktrees/unified-op-eval-integration`
- 分支名称: `refactor/unified-op-eval-integration`

---

## 涉及文件与职责规划

| 文件路径 | 动作 | 职责描述 |
|---|---|---|
| `include/cdsl/ast.h` | 修改 | 声明并实现 `static inline bool cdsl_op_eval_cmp(cdsl_op_t op, int cmp)` |
| `src/vm/value_ops.c` | 修改 | 移除私有算子谓词；将 6 种数据类型的关系比较统一重构为调用 `cdsl_op_eval_cmp` |
| `src/vm/internal.h` | 修改 | 声明 VM 内部双向算子映射 `cdsl_op_to_bc` 与 `cdsl_bc_to_op` |
| `src/vm/bytecode.c` | 修改 | 实现双向算子映射；精简编译期发射逻辑；合并 VM 运行期二元算子派发分支 |
| `src/vm/codegen.c` | 修改 | 移除局部 `op == CDSL_OP_EQ || ...` 及三元符号映射，改用公共谓词与 `cdsl_op_c_symbol` |
| `src/schema/schema.c` | 修改 | 在 `expr_is_const_true` 与 `expr_is_const_false` 中改用 `cdsl_op_eval_cmp` |
| `tests/test_ast.c` | 修改 | 新增 `cdsl_op_eval_cmp` 完备真值表与异常输入单测 |
| `tests/test_bytecode.c` | 修改 | 新增 `cdsl_op_to_bc` 与 `cdsl_bc_to_op` 双向往返单测 |

---

### Task 1: AST 层通用关系比较求值器 `cdsl_op_eval_cmp` 实现与单测

**Files:**
- Modify: `include/cdsl/ast.h`
- Test: `tests/test_ast.c`

- [ ] **Step 1: 在 `tests/test_ast.c` 中编写失败测试**

在 `tests/test_ast.c` 的 `main()` 前新增函数 `test_ast_op_eval_cmp()`：

```c
/**
 * @brief Test normalized three-way comparison operator evaluator.
 */
static void
test_ast_op_eval_cmp(void)
{
	TEST_BEGIN("operator three-way comparison evaluator");

	/* EQ: true only when cmp == 0 */
	TEST_ASSERT_INT(cdsl_op_eval_cmp(CDSL_OP_EQ, 0), 1, "EQ with cmp=0");
	TEST_ASSERT_INT(cdsl_op_eval_cmp(CDSL_OP_EQ, -1), 0, "EQ with cmp=-1");
	TEST_ASSERT_INT(cdsl_op_eval_cmp(CDSL_OP_EQ, 1), 0, "EQ with cmp=1");

	/* NE: true when cmp != 0 */
	TEST_ASSERT_INT(cdsl_op_eval_cmp(CDSL_OP_NE, 0), 0, "NE with cmp=0");
	TEST_ASSERT_INT(cdsl_op_eval_cmp(CDSL_OP_NE, -1), 1, "NE with cmp=-1");
	TEST_ASSERT_INT(cdsl_op_eval_cmp(CDSL_OP_NE, 1), 1, "NE with cmp=1");

	/* LT: true only when cmp < 0 */
	TEST_ASSERT_INT(cdsl_op_eval_cmp(CDSL_OP_LT, -1), 1, "LT with cmp=-1");
	TEST_ASSERT_INT(cdsl_op_eval_cmp(CDSL_OP_LT, 0), 0, "LT with cmp=0");
	TEST_ASSERT_INT(cdsl_op_eval_cmp(CDSL_OP_LT, 1), 0, "LT with cmp=1");

	/* GT: true only when cmp > 0 */
	TEST_ASSERT_INT(cdsl_op_eval_cmp(CDSL_OP_GT, 1), 1, "GT with cmp=1");
	TEST_ASSERT_INT(cdsl_op_eval_cmp(CDSL_OP_GT, 0), 0, "GT with cmp=0");
	TEST_ASSERT_INT(cdsl_op_eval_cmp(CDSL_OP_GT, -1), 0, "GT with cmp=-1");

	/* LE: true when cmp <= 0 */
	TEST_ASSERT_INT(cdsl_op_eval_cmp(CDSL_OP_LE, -1), 1, "LE with cmp=-1");
	TEST_ASSERT_INT(cdsl_op_eval_cmp(CDSL_OP_LE, 0), 1, "LE with cmp=0");
	TEST_ASSERT_INT(cdsl_op_eval_cmp(CDSL_OP_LE, 1), 0, "LE with cmp=1");

	/* GE: true when cmp >= 0 */
	TEST_ASSERT_INT(cdsl_op_eval_cmp(CDSL_OP_GE, 1), 1, "GE with cmp=1");
	TEST_ASSERT_INT(cdsl_op_eval_cmp(CDSL_OP_GE, 0), 1, "GE with cmp=0");
	TEST_ASSERT_INT(cdsl_op_eval_cmp(CDSL_OP_GE, -1), 0, "GE with cmp=-1");

	/* Non-relational and invalid operators return false */
	TEST_ASSERT_INT(cdsl_op_eval_cmp(CDSL_OP_ADD, 0), 0, "ADD returns false");
	TEST_ASSERT_INT(cdsl_op_eval_cmp(CDSL_OP_AND, 0), 0, "AND returns false");
	TEST_ASSERT_INT(cdsl_op_eval_cmp(CDSL_OP_NOT, 0), 0, "NOT returns false");
	TEST_ASSERT_INT(cdsl_op_eval_cmp((cdsl_op_t)999, 0), 0, "invalid op returns false");

	TEST_END();
}
```
并在 `main()` 中调用 `test_ast_op_eval_cmp();`。

- [ ] **Step 2: 编译测试以确认失败**

运行：`cmake --build build --target test_ast`
预期输出：编译失败，提示 `cdsl_op_eval_cmp` 未声明。

- [ ] **Step 3: 在 `include/cdsl/ast.h` 中实现 `cdsl_op_eval_cmp`**

在 `include/cdsl/ast.h` 的 `Operator and Type Queries` 区域追加：
```c
/**
 * @brief 根据三值比较结果 (-1: 小于, 0: 等于, 1: 大于) 计算关系算子布尔值
 *
 * 供统一求值层 (value_ops.c)、常量折叠与 Schema 校验等模块无开销内联复用。
 *
 * @param op 算子枚举常量 (必须为关系算子)
 * @param cmp 三值比较整数 (规则: cmp < 0 表示左小于右, cmp == 0 表示相等, cmp > 0 表示左大于右)
 * @return 关系是否成立；若 op 非关系比较算子则安全返回 false
 */
static inline bool
cdsl_op_eval_cmp(cdsl_op_t op, int cmp)
{
	switch (op) {
	case CDSL_OP_EQ:
		return cmp == 0;
	case CDSL_OP_NE:
		return cmp != 0;
	case CDSL_OP_LT:
		return cmp < 0;
	case CDSL_OP_GT:
		return cmp > 0;
	case CDSL_OP_LE:
		return cmp <= 0;
	case CDSL_OP_GE:
		return cmp >= 0;
	default:
		return false;
	}
}
```

- [ ] **Step 4: 编译并运行单测验证**

运行：`cmake --build build --target test_ast && ctest --test-dir build -R test_ast --output-on-failure`
预期输出：构建成功，零警告，`test_ast` 测试全部通过。

- [ ] **Step 5: 提交 Task 1 代码**

```bash
git add include/cdsl/ast.h tests/test_ast.c
git commit -m "feat(ast): ✨ add normalized three-way comparison operator evaluator cdsl_op_eval_cmp"
```

---

### Task 2: 统一求值层 (`src/vm/value_ops.c`) 瘦身与三值比较收敛

**Files:**
- Modify: `src/vm/value_ops.c`
- Test: `tests/test_execution.c`, `tests/test_ast.c`

- [ ] **Step 1: 清理本地私有算子谓词**

在 `src/vm/value_ops.c` 中：
- 删除 `static int is_comparison_op(cdsl_op_t op)`
- 删除 `static int is_arithmetic_op(cdsl_op_t op)`
- 全文替换 `is_comparison_op(op)` 为 `cdsl_op_is_relational(op)`
- 全文替换 `is_arithmetic_op(op)` 为 `cdsl_op_is_arithmetic(op)`

- [ ] **Step 2: 重构 6 类数据类型的关系比较**

将 `src/vm/value_ops.c` 中的关系比较重构为计算 `int cmp` 并调用 `cdsl_op_eval_cmp(op, cmp)`：
1. **STRING**:
   ```c
   int cmp = strcmp(a->data.string_val ? a->data.string_val : "",
                    b->data.string_val ? b->data.string_val : "");
   out->type = CDSL_TYPE_BOOL;
   out->data.bool_val = cdsl_op_eval_cmp(op, cmp);
   return CDSL_VALOP_OK;
   ```
2. **DATE**:
   ```c
   if (cdsl_op_is_relational(op)) {
       time_t t1 = a->data.date_val;
       time_t t2 = b->data.date_val;
       int cmp = (t1 < t2) ? -1 : (t1 > t2) ? 1 : 0;
       out->type = CDSL_TYPE_BOOL;
       out->data.bool_val = cdsl_op_eval_cmp(op, cmp);
       return CDSL_VALOP_OK;
   }
   ```
3. **BOOL**:
   ```c
   int bv1 = a->data.bool_val ? 1 : 0;
   int bv2 = b->data.bool_val ? 1 : 0;
   int cmp = (bv1 < bv2) ? -1 : (bv1 > bv2) ? 1 : 0;
   out->type = CDSL_TYPE_BOOL;
   out->data.bool_val = cdsl_op_eval_cmp(op, cmp);
   return CDSL_VALOP_OK;
   ```
4. **FLOAT**:
   ```c
   if (cdsl_op_is_relational(op)) {
       int cmp = (fabs(av - bv) < 1e-9) ? 0 : (av < bv) ? -1 : 1;
       out->type = CDSL_TYPE_BOOL;
       out->data.bool_val = cdsl_op_eval_cmp(op, cmp);
       return CDSL_VALOP_OK;
   }
   ```
5. **LONG**:
   ```c
   if (cdsl_op_is_relational(op)) {
       int cmp = (al < bl) ? -1 : (al > bl) ? 1 : 0;
       out->type = CDSL_TYPE_BOOL;
       out->data.bool_val = cdsl_op_eval_cmp(op, cmp);
       return CDSL_VALOP_OK;
   }
   ```
6. **INT**:
   ```c
   if (cdsl_op_is_relational(op)) {
       int cmp = (ai < bi) ? -1 : (ai > bi) ? 1 : 0;
       out->type = CDSL_TYPE_BOOL;
       out->data.bool_val = cdsl_op_eval_cmp(op, cmp);
       return CDSL_VALOP_OK;
   }
   ```

- [ ] **Step 3: 编译并运行回归测试**

运行：
```bash
cmake --build build --target test_ast test_execution test_differential
ctest --test-dir build -R "test_ast|test_execution|test_differential" --output-on-failure
```
预期输出：零警告，测试全部通过。

- [ ] **Step 4: 提交 Task 2 代码**

```bash
git add src/vm/value_ops.c
git commit -m "refactor(vm): ♻️ unify value operations comparison using cdsl_op_eval_cmp"
```

---

### Task 3: 字节码双向算子映射与 VM 二元算子执行块合并

**Files:**
- Modify: `src/vm/internal.h`
- Modify: `src/vm/bytecode.c`
- Test: `tests/test_bytecode.c`

- [ ] **Step 1: 在 `tests/test_bytecode.c` 中编写失败测试**

在 `tests/test_bytecode.c` 中增加 `test_bytecode_op_bidirectional_mapping()`：
```c
static void
test_bytecode_op_bidirectional_mapping(void)
{
	TEST_BEGIN("bytecode operator bidirectional mapping");

	/* Relational */
	TEST_ASSERT_INT(cdsl_op_to_bc(CDSL_OP_EQ), BC_EQ, "to BC_EQ");
	TEST_ASSERT_INT(cdsl_bc_to_op(BC_EQ), CDSL_OP_EQ, "to OP_EQ");
	TEST_ASSERT_INT(cdsl_op_to_bc(CDSL_OP_NE), BC_NE, "to BC_NE");
	TEST_ASSERT_INT(cdsl_bc_to_op(BC_NE), CDSL_OP_NE, "to OP_NE");
	TEST_ASSERT_INT(cdsl_op_to_bc(CDSL_OP_LT), BC_LT, "to BC_LT");
	TEST_ASSERT_INT(cdsl_bc_to_op(BC_LT), CDSL_OP_LT, "to OP_LT");
	TEST_ASSERT_INT(cdsl_op_to_bc(CDSL_OP_GT), BC_GT, "to BC_GT");
	TEST_ASSERT_INT(cdsl_bc_to_op(BC_GT), CDSL_OP_GT, "to OP_GT");
	TEST_ASSERT_INT(cdsl_op_to_bc(CDSL_OP_LE), BC_LE, "to BC_LE");
	TEST_ASSERT_INT(cdsl_bc_to_op(BC_LE), CDSL_OP_LE, "to OP_LE");
	TEST_ASSERT_INT(cdsl_op_to_bc(CDSL_OP_GE), BC_GE, "to BC_GE");
	TEST_ASSERT_INT(cdsl_bc_to_op(BC_GE), CDSL_OP_GE, "to OP_GE");

	/* Arithmetic */
	TEST_ASSERT_INT(cdsl_op_to_bc(CDSL_OP_ADD), BC_ADD, "to BC_ADD");
	TEST_ASSERT_INT(cdsl_bc_to_op(BC_ADD), CDSL_OP_ADD, "to OP_ADD");
	TEST_ASSERT_INT(cdsl_op_to_bc(CDSL_OP_SUB), BC_SUB, "to BC_SUB");
	TEST_ASSERT_INT(cdsl_bc_to_op(BC_SUB), CDSL_OP_SUB, "to OP_SUB");
	TEST_ASSERT_INT(cdsl_op_to_bc(CDSL_OP_MUL), BC_MUL, "to BC_MUL");
	TEST_ASSERT_INT(cdsl_bc_to_op(BC_MUL), CDSL_OP_MUL, "to OP_MUL");
	TEST_ASSERT_INT(cdsl_op_to_bc(CDSL_OP_DIV), BC_DIV, "to BC_DIV");
	TEST_ASSERT_INT(cdsl_bc_to_op(BC_DIV), CDSL_OP_DIV, "to OP_DIV");

	/* Unary */
	TEST_ASSERT_INT(cdsl_op_to_bc(CDSL_OP_NOT), BC_NOT, "to BC_NOT");
	TEST_ASSERT_INT(cdsl_bc_to_op(BC_NOT), CDSL_OP_NOT, "to OP_NOT");
	TEST_ASSERT_INT(cdsl_op_to_bc(CDSL_OP_NEG), BC_NEG, "to BC_NEG");
	TEST_ASSERT_INT(cdsl_bc_to_op(BC_NEG), CDSL_OP_NEG, "to OP_NEG");

	/* Non-mapped and boundary */
	TEST_ASSERT_INT(cdsl_op_to_bc(CDSL_OP_AND), BC_NOP, "AND has no direct opcode");
	TEST_ASSERT_INT(cdsl_op_to_bc(CDSL_OP_OR), BC_NOP, "OR has no direct opcode");
	TEST_ASSERT_INT(cdsl_op_to_bc((cdsl_op_t)999), BC_NOP, "invalid op to BC_NOP");
	TEST_ASSERT_INT(cdsl_bc_to_op(BC_NOP), (cdsl_op_t)-1, "BC_NOP to -1");
	TEST_ASSERT_INT(cdsl_bc_to_op((bc_opcode_t)999), (cdsl_op_t)-1, "invalid bcop to -1");

	TEST_END();
}
```
并在 `main()` 中调用。

- [ ] **Step 2: 编译以确认失败**

运行：`cmake --build build --target test_bytecode`
预期输出：编译失败，提示 `cdsl_op_to_bc` / `cdsl_bc_to_op` 未声明。

- [ ] **Step 3: 在 `src/vm/internal.h` 和 `src/vm/bytecode.c` 中声明并实现映射函数**

在 `src/vm/internal.h` 增加声明：
```c
bc_opcode_t cdsl_op_to_bc(cdsl_op_t op);
cdsl_op_t cdsl_bc_to_op(bc_opcode_t bcop);
```

在 `src/vm/bytecode.c` 实现：
```c
bc_opcode_t
cdsl_op_to_bc(cdsl_op_t op)
{
	switch (op) {
	case CDSL_OP_EQ:  return BC_EQ;
	case CDSL_OP_NE:  return BC_NE;
	case CDSL_OP_LT:  return BC_LT;
	case CDSL_OP_GT:  return BC_GT;
	case CDSL_OP_LE:  return BC_LE;
	case CDSL_OP_GE:  return BC_GE;
	case CDSL_OP_ADD: return BC_ADD;
	case CDSL_OP_SUB: return BC_SUB;
	case CDSL_OP_MUL: return BC_MUL;
	case CDSL_OP_DIV: return BC_DIV;
	case CDSL_OP_NOT: return BC_NOT;
	case CDSL_OP_NEG: return BC_NEG;
	default:          return BC_NOP;
	}
}

cdsl_op_t
cdsl_bc_to_op(bc_opcode_t bcop)
{
	switch (bcop) {
	case BC_EQ:  return CDSL_OP_EQ;
	case BC_NE:  return CDSL_OP_NE;
	case BC_LT:  return CDSL_OP_LT;
	case BC_GT:  return CDSL_OP_GT;
	case BC_LE:  return CDSL_OP_LE;
	case BC_GE:  return CDSL_OP_GE;
	case BC_ADD: return CDSL_OP_ADD;
	case BC_SUB: return CDSL_OP_SUB;
	case BC_MUL: return CDSL_OP_MUL;
	case BC_DIV: return CDSL_OP_DIV;
	case BC_NOT: return CDSL_OP_NOT;
	case BC_NEG: return CDSL_OP_NEG;
	default:     return (cdsl_op_t)-1;
	}
}
```

- [ ] **Step 4: 重构 `compile_expr` 与 `cdsl_bytecode_run`**

在 `compile_expr`（原第 522-549 行）简化为：
```c
	default: {
		bc_opcode_t bcop = cdsl_op_to_bc(op);
		if (bcop != BC_NOP) {
			bc_emit(bc, bcop);
		}
		break;
	}
```

在 `cdsl_bytecode_run` 将 `BC_ADD .. BC_DIV` 与 `BC_EQ .. BC_GE` 合并为一个 case 块：
```c
	/* Unified Binary Arithmetic and Comparison Operations */
	case BC_ADD:
	case BC_SUB:
	case BC_MUL:
	case BC_DIV:
	case BC_EQ:
	case BC_NE:
	case BC_LT:
	case BC_GT:
	case BC_LE:
	case BC_GE: {
		cdsl_value_t b = POP();
		cdsl_value_t a = POP();
		if (CDSL_ATOMIC_LOAD(&vm->error_state)) {
			cdsl_value_free(&a);
			cdsl_value_free(&b);
			result.type = CDSL_TYPE_VOID;
			goto done;
		}
		cdsl_op_t op = cdsl_bc_to_op(ip->op);
		cdsl_valop_status_t st = cdsl_value_binop(op, &a, &b, &result);
		cdsl_value_free(&a);
		cdsl_value_free(&b);
		if (st != CDSL_VALOP_OK && st != CDSL_VALOP_VOID_OPERAND) {
			if (!CDSL_ATOMIC_LOAD(&vm->error_state)) {
				cdsl_vm_set_last_error(vm,
						       CDSL_ERR_RUNTIME,
						       0,
						       0,
						       cdsl_valop_status_message(st),
						       cdsl_valop_status_hint(st));
				CDSL_ATOMIC_STORE(&vm->error_state, 1);
			}
			result.type = CDSL_TYPE_VOID;
			goto done;
		}
		PUSH(result);
		ip++;
		break;
	}
```

- [ ] **Step 5: 编译并运行单测验证**

运行：
```bash
cmake --build build --target test_bytecode test_differential
ctest --test-dir build -R "test_bytecode|test_differential" --output-on-failure
```
预期输出：零警告，测试全部通过。

- [ ] **Step 6: 提交 Task 3 代码**

```bash
git add src/vm/internal.h src/vm/bytecode.c tests/test_bytecode.c
git commit -m "refactor(bytecode): ♻️ implement bidirectional op mapping and unify binary execution loop"
```

---

### Task 4: 周边消费模块精简 (`codegen.c` & `schema.c`)

**Files:**
- Modify: `src/vm/codegen.c`
- Modify: `src/schema/schema.c`
- Test: `tests/test_codegen.c`, `tests/test_schema.c`

- [ ] **Step 1: 精简 `src/vm/codegen.c`**

在 `codegen_expr` 中替换第 169-183 行：
```c
		cdsl_type_t lt = expr_get_type(expr->data.binary.left, schema);
		cdsl_type_t rt = expr_get_type(expr->data.binary.right, schema);
		int is_str = (lt == CDSL_TYPE_STRING || rt == CDSL_TYPE_STRING);
		cdsl_op_t op = expr->data.binary.op;
		if (is_str && cdsl_op_is_relational(op)) {
			const char* cmp_op = cdsl_op_c_symbol(op);
			cdsl_strbuf_printf(sb, "(strcmp(");
			codegen_expr(sb, expr->data.binary.left, schema, indent);
			cdsl_strbuf_printf(sb, ", ");
			codegen_expr(sb, expr->data.binary.right, schema, indent);
			cdsl_strbuf_printf(sb, ") %s 0)", cmp_op);
			break;
		}
```

- [ ] **Step 2: 精简 `src/schema/schema.c`**

在 `expr_is_const_true` 与 `expr_is_const_false` 中替换第 545-605 行：
```c
		if (l->type != CDSL_EXPR_INT || r->type != CDSL_EXPR_INT) {
			return 0;
		}
		if (cdsl_op_is_relational(op)) {
			int cmp = (l->data.int_val < r->data.int_val) ? -1 : (l->data.int_val > r->data.int_val) ? 1 : 0;
			return cdsl_op_eval_cmp(op, cmp) ? 1 : 0;
		}
		return 0;
```
与
```c
		if (l->type != CDSL_EXPR_INT || r->type != CDSL_EXPR_INT) {
			return 0;
		}
		if (cdsl_op_is_relational(op)) {
			int cmp = (l->data.int_val < r->data.int_val) ? -1 : (l->data.int_val > r->data.int_val) ? 1 : 0;
			return cdsl_op_eval_cmp(op, cmp) ? 0 : 1;
		}
		return 0;
```

- [ ] **Step 3: 编译并运行测试**

运行：
```bash
cmake --build build --target test_codegen test_schema
ctest --test-dir build -R "test_codegen|test_schema" --output-on-failure
```
预期输出：零警告，测试全部通过。

- [ ] **Step 4: 提交 Task 4 代码**

```bash
git add src/vm/codegen.c src/schema/schema.c
git commit -m "refactor: ♻️ streamline relational operator handling in codegen and schema"
```

---

### Task 5: 全量回归测试与分支合并

**Files:**
- 全量构建与回归测试

- [ ] **Step 1: 全量构建并运行 28 项 CTest 测试**

```bash
cmake --build build
ctest --test-dir build --output-on-failure
```
预期输出：28/28 测试 100% 通过。

- [ ] **Step 2: 内存安全与动态分析验证**

运行 ASan / UBSan 构建或内存追踪测试，确保无内存泄漏和非法越界。

- [ ] **Step 3: 合并工作分支并清理 Worktree**

```bash
git checkout master
git merge --no-ff refactor/unified-op-eval-integration -m "merge: 🔀 merge refactor/unified-op-eval-integration into master"
git worktree remove .worktrees/unified-op-eval-integration
git branch -d refactor/unified-op-eval-integration
ctest --test-dir build --output-on-failure
```
确保主分支处于最新且干净状态。
