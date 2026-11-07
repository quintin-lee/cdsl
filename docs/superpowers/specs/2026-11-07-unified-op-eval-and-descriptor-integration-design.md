# 设计文档：算子描述符表与统一求值层深度整合设计

**日期**: 2026-11-07
**项目**: C-DSL (cdsl)
**作者**: Antigravity & User Pair Programming
**状态**: 待审核 (Ready for Review)

---

## 1. 背景与问题分析

在完成算子元数据集中描述符表（`cdsl_op_desc_t` 与 `s_op_table`）以及值语义改造后，AST 层在单一真实数据源（Single Source of Truth）方面取得了长足进步。然而，深入审查全库代码后发现，在**统一求值层 (`value_ops.c`)**、**字节码执行引擎 (`bytecode.c`)**、**代码生成器 (`codegen.c`)** 与 **语法模式校验 (`schema.c`)** 之间，仍然存在大量未打通的重复模板代码与算子判定分支：

### 1.1 关系算子多路判断在各类型中重复展开了 8 次
在 [`src/vm/value_ops.c`](file:///home/quintin/Data/source/c_cpp/cdsl/src/vm/value_ops.c) 中，对不同数据类型（`STRING`, `DATE`, `BOOL`, `FLOAT`, `LONG`, `INT`）做大小与相等比较时，完全相同的 6 路三元运算符级联被手工复制了 6 次；而在 [`src/schema/schema.c`](file:///home/quintin/Data/source/c_cpp/cdsl/src/schema/schema.c#L549-L605) 的常量折叠判定中又各复制了 2 次：
```c
// 散落在 8 个不同位置的重复三元逻辑：
out->data.bool_val = (op == CDSL_OP_EQ) ? (cmp == 0)
                   : (op == CDSL_OP_NE) ? (cmp != 0)
                   : (op == CDSL_OP_LT) ? (cmp < 0)
                   : (op == CDSL_OP_GT) ? (cmp > 0)
                   : (op == CDSL_OP_LE) ? (cmp <= 0)
                                        : (cmp >= 0);
```
一旦未来对浮点精度容差、边界比较规则进行微调，必须在 8 个地方同步修改，极易遗漏引发静默不一致。

### 1.2 本地重复定义算子谓词与符号映射
- 在 [`src/vm/value_ops.c`](file:///home/quintin/Data/source/c_cpp/cdsl/src/vm/value_ops.c#L69-L79) 中，手工定义了静态函数 `is_comparison_op(op)` 与 `is_arithmetic_op(op)`，未复用 [`include/cdsl/ast.h`](file:///home/quintin/Data/source/c_cpp/cdsl/include/cdsl/ast.h) 中的统一谓词 [`cdsl_op_is_relational`](file:///home/quintin/Data/source/c_cpp/cdsl/include/cdsl/ast.h) 与 [`cdsl_op_is_arithmetic`](file:///home/quintin/Data/source/c_cpp/cdsl/include/cdsl/ast.h)。
- 在 [`src/vm/codegen.c`](file:///home/quintin/Data/source/c_cpp/cdsl/src/vm/codegen.c#L169-L176) 中，使用一长串 `op == CDSL_OP_EQ || op == CDSL_OP_NE ...` 判定关系运算，并手动写 switch 映射 `cmp_op`，而这正是统一算子表 [`cdsl_op_c_symbol(op)`](file:///home/quintin/Data/source/c_cpp/cdsl/include/cdsl/ast.h) 已提供的能力。

### 1.3 字节码 VM 中算术与关系指令派发高度重复 (~90 行样板代码)
在 [`src/vm/bytecode.c`](file:///home/quintin/Data/source/c_cpp/cdsl/src/vm/bytecode.c#L1070-L1159) 中，`BC_ADD .. BC_DIV`（算术）和 `BC_EQ .. BC_GE`（比较）两处分别写了一个 `switch (ip->op)` 将字节码映射为 `cdsl_op_t`，随后的出栈、调用 [`cdsl_value_binop`](file:///home/quintin/Data/source/c_cpp/cdsl/src/vm/value_ops.h)、错误处理、入栈、递增 IP 等代码**100% 逐字重复**。同时，AST 编译到字节码的发射阶段（第 522-549 行）也维护了一份 30 行的手工 `switch` 分支。

---

## 2. 目标与设计原则

1. **统一关系比较契约 (Unified Comparison Contract)**：在 AST 层建立标准化的三值关系比较求值器 `cdsl_op_eval_cmp`，实现零开销内联，消除全库 8 处重复的三元级联分支。
2. **算子元数据真实数据源唯一性 (Single Source of Truth)**：彻底消除 `value_ops.c` 和 `codegen.c` 中的私有算子谓词与局部 C 符号映射，全面收敛至 `cdsl_op_desc_t` 接口。
3. **VM 字节码双向契约与二元操作合并**：在 VM 内部建立紧凑的高效双向映射（`cdsl_op_to_bc` 与 `cdsl_bc_to_op`），将编译发射精简为单行分派，将 VM 运行循环中的二元算术与比较指令合并为单一执行流，削除 ~70 行重复样板代码。
4. **编译与回归安全**：保持 `-Wall -Wextra -Werror` 零警告，全库 28 项测试套件 100% 通过，确保运行时性能零衰减。

---

## 3. 详细设计

### 3.1 AST 层通用关系比较求值与元数据增强 (`include/cdsl/ast.h`)

#### 3.1.1 接口声明与内联实现
在 [`include/cdsl/ast.h`](file:///home/quintin/Data/source/c_cpp/cdsl/include/cdsl/ast.h) 的 `Operator and Type Queries` 区域追加：

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

#### 3.1.2 边界与安全性设计
- **三值输入容错**：只要 `cmp < 0` 统一判定为小于，`cmp == 0` 判定为等于，`cmp > 0` 判定为大于，直接完美适配 `strcmp` 返回值与数值差值符号判定。
- **非法算子容错**：非关系算子或未知枚举值进入 `default`，安全返回 `false`。
- **编译期优化**：`static inline` 保障在开启 `-O2` 时直接生成条件传送或设置指令（`sete`/`setle`/`setg`），零栈调用开销。

---

### 3.2 统一求值层重构与三值比较收敛 (`src/vm/value_ops.c`)

#### 3.2.1 清除局部重复谓词定义
在 [`src/vm/value_ops.c`](file:///home/quintin/Data/source/c_cpp/cdsl/src/vm/value_ops.c) 中：
- 移除 `static int is_comparison_op(cdsl_op_t op);`
- 移除 `static int is_arithmetic_op(cdsl_op_t op);`
- 替换为公共的 [`cdsl_op_is_relational(op)`](file:///home/quintin/Data/source/c_cpp/cdsl/include/cdsl/ast.h) 与 [`cdsl_op_is_arithmetic(op)`](file:///home/quintin/Data/source/c_cpp/cdsl/include/cdsl/ast.h)。

#### 3.2.2 统一三值比较模式（Normalized Three-Way Comparison）
所有数据类型的关系比较全部收敛为两阶段标准范式：
1. **阶段 1：提取三值标量 `cmp`**（严格保证 `-1: 左小, 0: 等于, 1: 左大`）；
2. **阶段 2：单行委派 `cdsl_op_eval_cmp(op, cmp)`**。

```c
/* STRING */
if (a->type == CDSL_TYPE_STRING || b->type == CDSL_TYPE_STRING) {
    if (a->type != CDSL_TYPE_STRING || b->type != CDSL_TYPE_STRING) {
        return CDSL_VALOP_TYPE_ERROR;
    }
    if (op != CDSL_OP_EQ && op != CDSL_OP_NE) {
        return CDSL_VALOP_TYPE_ERROR;
    }
    int cmp = strcmp(a->data.string_val ? a->data.string_val : "",
                     b->data.string_val ? b->data.string_val : "");
    out->type = CDSL_TYPE_BOOL;
    out->data.bool_val = cdsl_op_eval_cmp(op, cmp);
    return CDSL_VALOP_OK;
}

/* DATE */
if (a->type == CDSL_TYPE_DATE && b->type == CDSL_TYPE_DATE) {
    ...
    if (cdsl_op_is_relational(op)) {
        time_t t1 = a->data.date_val;
        time_t t2 = b->data.date_val;
        int cmp = (t1 < t2) ? -1 : (t1 > t2) ? 1 : 0;
        out->type = CDSL_TYPE_BOOL;
        out->data.bool_val = cdsl_op_eval_cmp(op, cmp);
        return CDSL_VALOP_OK;
    }
    return CDSL_VALOP_TYPE_ERROR;
}

/* BOOL */
if (a->type == CDSL_TYPE_BOOL || b->type == CDSL_TYPE_BOOL) {
    if (a->type != CDSL_TYPE_BOOL || b->type != CDSL_TYPE_BOOL || !cdsl_op_is_relational(op)) {
        return CDSL_VALOP_TYPE_ERROR;
    }
    int bv1 = a->data.bool_val ? 1 : 0;
    int bv2 = b->data.bool_val ? 1 : 0;
    int cmp = (bv1 < bv2) ? -1 : (bv1 > bv2) ? 1 : 0;
    out->type = CDSL_TYPE_BOOL;
    out->data.bool_val = cdsl_op_eval_cmp(op, cmp);
    return CDSL_VALOP_OK;
}

/* FLOAT */
if (cdsl_op_is_relational(op)) {
    int cmp = (fabs(av - bv) < 1e-9) ? 0 : (av < bv) ? -1 : 1;
    out->type = CDSL_TYPE_BOOL;
    out->data.bool_val = cdsl_op_eval_cmp(op, cmp);
    return CDSL_VALOP_OK;
}

/* LONG */
if (cdsl_op_is_relational(op)) {
    int cmp = (al < bl) ? -1 : (al > bl) ? 1 : 0;
    out->type = CDSL_TYPE_BOOL;
    out->data.bool_val = cdsl_op_eval_cmp(op, cmp);
    return CDSL_VALOP_OK;
}

/* INT */
if (cdsl_op_is_relational(op)) {
    int cmp = (ai < bi) ? -1 : (ai > bi) ? 1 : 0;
    out->type = CDSL_TYPE_BOOL;
    out->data.bool_val = cdsl_op_eval_cmp(op, cmp);
    return CDSL_VALOP_OK;
}
```

---

### 3.3 字节码双向映射与 VM 执行块合并 (`src/vm/bytecode.c` & `src/vm/internal.h`)

#### 3.3.1 内部双向转换契约 (`src/vm/internal.h`)
```c
/**
 * @brief 将 AST 算子枚举转换为字节码操作码
 * @param op 算子枚举
 * @return 对应的字节码操作码；若非直接指令 (如短路逻辑 AND/OR) 则返回 BC_NOP
 */
bc_opcode_t cdsl_op_to_bc(cdsl_op_t op);

/**
 * @brief 将字节码操作码转换为对应的 AST 算子枚举
 * @param bcop 字节码操作码 (BC_ADD..BC_DIV, BC_EQ..BC_GE, BC_NOT, BC_NEG)
 * @return 对应的 AST 算子枚举；非法返回 (cdsl_op_t)-1
 */
cdsl_op_t cdsl_bc_to_op(bc_opcode_t bcop);
```

#### 3.3.2 映射实现 (`src/vm/bytecode.c`)
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

#### 3.3.3 编译发射层精简
在 `compile_expr` 中替换第 522-549 行的大段 switch：
```c
	default: {
		bc_opcode_t bcop = cdsl_op_to_bc(op);
		if (bcop != BC_NOP) {
			bc_emit(bc, bcop);
		}
		break;
	}
```

#### 3.3.4 VM 运行期二元算子执行块合并
在 `cdsl_bytecode_run` 中，合并 `BC_ADD .. BC_DIV` 与 `BC_EQ .. BC_GE`：
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

---

### 3.4 周边消费模块精简 (`codegen.c` & `schema.c`)

1. **`src/vm/codegen.c`**：
   ```c
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
2. **`src/schema/schema.c`**：
   ```c
   /* In expr_is_const_true */
   if (l->type != CDSL_EXPR_INT || r->type != CDSL_EXPR_INT) {
       return 0;
   }
   if (cdsl_op_is_relational(op)) {
       int cmp = (l->data.int_val < r->data.int_val) ? -1 : (l->data.int_val > r->data.int_val) ? 1 : 0;
       return cdsl_op_eval_cmp(op, cmp) ? 1 : 0;
   }
   return 0;

   /* In expr_is_const_false */
   if (l->type != CDSL_EXPR_INT || r->type != CDSL_EXPR_INT) {
       return 0;
   }
   if (cdsl_op_is_relational(op)) {
       int cmp = (l->data.int_val < r->data.int_val) ? -1 : (l->data.int_val > r->data.int_val) ? 1 : 0;
       return cdsl_op_eval_cmp(op, cmp) ? 0 : 1;
   }
   return 0;
   ```

---

## 4. 收益与代码度量

| 模块 | 修改前状态 | 修改后收益 |
|---|---|---|
| **`value_ops.c`** | 6 组完全相同的关系运算级联，私有谓词 | 统一收敛为提取 `cmp` 后委派，削减 ~40 行 |
| **`bytecode.c`** | 编译 30 行 switch，执行 ~90 行重复代码 | 编译 5 行，执行合并为一个 case，削减 ~75 行 |
| **`codegen.c`** | 手写 6 组关系判断与私有三元映射 | 2 行调用公共描述符与符号，削减 8 行 |
| **`schema.c`** | 2 组 15 行的常量比较 switch-case | 统一调用 `cdsl_op_eval_cmp`，削减 30 行 |
| **全库净削减** | **净减少 ~150 行冗余样板代码** | 消除多头维护，提升代码一致性与紧凑度 |

---

## 5. 验证与测试方案

1. **测试驱动开发 (TDD)**：
   - 在 [`tests/test_ast.c`](file:///home/quintin/Data/source/c_cpp/cdsl/tests/test_ast.c) 中编写 `test_ast_op_eval_cmp()`，覆盖关系算子真值表与非法算子防御；
   - 在 [`tests/test_bytecode.c`](file:///home/quintin/Data/source/c_cpp/cdsl/tests/test_bytecode.c) 中编写 `test_bytecode_op_bidirectional_mapping()`，覆盖全部 12 个操作码的双向往返转换。
2. **构建与全量回归**：
   - `-Wall -Wextra -Werror` 零编译警告；
   - CTest 全量 28 个测试套件 100% 通过（包含 AST 与 Bytecode 差异化等价测试 `test_differential`）。
