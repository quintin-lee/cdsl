# 设计文档：浮点数严格等值比较回归与 approx_eq 内置函数设计

**日期**: 2026-11-07  
**项目**: C-DSL (cdsl)  
**作者**: Antigravity & User Pair Programming  
**状态**: 待审核 (Ready for Review)  

---

## 1. 背景与问题分析

在 C-DSL 规则引擎中，浮点数运算贯穿了规则前置条件评估、指标打分（Metrics）与业务阈值判定。审查当前代码实现，浮点数的等值与不等值比较存在以下核心缺陷：

### 1.1 核心运算层硬编码绝对容差
在 [`src/vm/value_ops.c`](file:///home/quintin/Data/source/c_cpp/cdsl/src/vm/value_ops.c#L255-L264) 中，涉及浮点类型的 `CDSL_OP_EQ`（`==`）与 `CDSL_OP_NE`（`!=`）运算采用了硬编码的绝对容差 $10^{-9}$：
```c
if (is_comparison_op(op)) {
    out->type = CDSL_TYPE_BOOL;
    out->data.bool_val = (op == CDSL_OP_EQ)  ? (fabs(av - bv) < 1e-9)
                         : (op == CDSL_OP_NE) ? (fabs(av - bv) >= 1e-9)
                         : (op == CDSL_OP_LT) ? (av < bv)
                         : (op == CDSL_OP_GT) ? (av > bv)
                         : (op == CDSL_OP_LE) ? (av <= bv)
                                              : (av >= bv);
    return CDSL_VALOP_OK;
}
```

### 1.2 数值尺度与边界缺陷
1. **大数假阴性**：双精度浮点（IEEE 754 `double`）有效尾数为 53 位（约 15~17 位十进制有效数字）。当数值大于 $10^{10}$ 时（如 $10^{12}$），两个理论相等的值由于浮点微小舍入误差相差 $10^{-4}$，远超 $10^{-9}$，导致判定为不相等；
2. **小数假阳性**：当数值处于微观量级（如 $10^{-10}$ 与 $2 \times 10^{-10}$），两者相对误差高达 100%，但在绝对容差 $|a - b| = 10^{-10} < 10^{-9}$ 下却被误判为相等；
3. **特殊值未合规**：IEEE 754 规定 `NaN == NaN` 必须为 `false`，但纯差值绝对值公式在遇到 `Inf`（无穷大）时，`fabs(Inf - Inf)` 为 `NaN`，导致 `Inf == Inf` 判定失真。

### 1.3 跨执行后端语义不一致
- **解释执行（Tree-walk）与字节码 VM**：调用 `cdsl_value_binop()`，采用 $10^{-9}$ 容差判定；
- **C 代码生成器（Codegen）**：在 [`src/vm/codegen.c`](file:///home/quintin/Data/source/c_cpp/cdsl/src/vm/codegen.c#L187) 中直接生成 `(a == b)`，执行严格的 IEEE 754 原生等值比较；
- **结论**：相同的一条规则代码，在解释模式下 `0.1 + 0.2 == 0.3` 判定为 `true`，而编译生成 C 代码后判定为 `false`，破坏了执行引擎的同构一致性保证。

---

## 2. 目标与设计原则

1. **运算符职责纯粹化 (Strict IEEE 754 Semantics)**：语言核心比较算子 `==` 和 `!=` 回归严格的 IEEE 754 语义，彻底消除核心求值函数中的硬编码魔数与隐式黑盒容差。
2. **多端语义 100% 同构 (Cross-Engine Homomorphism)**：AST 树遍历解释器、Bytecode 虚拟机（包含编译期常量折叠）与 Codegen 代码生成器三端语义达到 1:1 绝对一致。
3. **显式模糊匹配能力 (Explicit Built-in `approx_eq`)**：提供标准内置函数 `approx_eq(a, b, [eps])`，将容差判断的主动权交还给规则编写者。
4. **业界标准混合容差算法 (Hybrid Tolerance Algorithm)**：在 `approx_eq` 中采用综合绝对容差与相对容差的稳健算法，兼顾大数防溢出与小数防穿透。
5. **安全与零存量破坏**：严格处理 `NaN` 与 `Inf` 等特殊浮点状态，全库既有测试套件 100% 保持通过。

---

## 3. 详细设计

### 3.1 运算符求值层改造 (`src/vm/value_ops.c`)

在 `src/vm/value_ops.c` 的浮点操作分支中，将 `CDSL_OP_EQ` 与 `CDSL_OP_NE` 的实现直接转换为 C 语言原生标准浮点等值运算：

```c
if (is_comparison_op(op)) {
    out->type = CDSL_TYPE_BOOL;
    out->data.bool_val = (op == CDSL_OP_EQ)  ? (av == bv)
                         : (op == CDSL_OP_NE) ? (av != bv)
                         : (op == CDSL_OP_LT) ? (av < bv)
                         : (op == CDSL_OP_GT) ? (av > bv)
                         : (op == CDSL_OP_LE) ? (av <= bv)
                                              : (av >= bv);
    return CDSL_VALOP_OK;
}
```

**语义行为矩阵**：
- `3.14 == 3.14` $\rightarrow$ `true`
- `0.1 + 0.2 == 0.3` $\rightarrow$ `false`（遵循 IEEE 754）
- `+0.0 == -0.0` $\rightarrow$ `true`
- `NaN == NaN` $\rightarrow$ `false`
- `NaN != NaN` $\rightarrow$ `true`
- `+Inf == +Inf` $\rightarrow$ `true`
- 浮点与整型（`FLOAT` 与 `INT`/`LONG`）比较时：整型自动提升为 `double` 后执行 IEEE 754 等值比较。

---

### 3.2 默认容差常量规范 (`include/cdsl/vm.h`)

在公共头文件 [`include/cdsl/vm.h`](file:///home/quintin/Data/source/c_cpp/cdsl/include/cdsl/vm.h) 中暴露官方默认容差宏定义：

```c
/**
 * @brief Default epsilon for approximate floating-point equality comparisons.
 *
 * Used by the built-in approx_eq(a, b) function when the third argument (eps)
 * is omitted.
 */
#define CDSL_DEFAULT_FLOAT_EPSILON 1e-9
```

---

### 3.3 `approx_eq` 内置函数设计与实现 (`src/vm/builtins.c`)

#### 3.3.1 函数签名与参数解构
在 [`src/vm/builtins.c`](file:///home/quintin/Data/source/c_cpp/cdsl/src/vm/builtins.c) 中新增 `builtin_approx_eq`，并在 `cdsl_vm_register_builtins()` 中完成自动注册：
```c
cdsl_vm_register_function(vm, "approx_eq", builtin_approx_eq);
```

- **参数形式**：
  - 双参：`approx_eq(a, b)` $\rightarrow$ 默认 `eps = CDSL_DEFAULT_FLOAT_EPSILON`；
  - 三参：`approx_eq(a, b, eps)` $\rightarrow$ 自定义容差 `eps`。
- **参数合法性校验**：
  - 若参数个数 $< 2$ 或 $> 3$，返回布尔值 `0`（`false`）；
  - 若 `a` 或 `b` 无法转换为数值类型（非 `FLOAT`/`INT`/`LONG`/`BOOL`），返回布尔值 `0`；
  - 若 `eps < 0.0`，返回布尔值 `0`。

#### 3.3.2 混合容差算法实现
采用类似 Python `math.isclose` 与 NumPy `isclose` 的混合容差模型：
$$\text{diff} = |a - b|$$
$$\text{tol} = \max(\text{eps}, \text{eps} \times \max(|a|, |b|))$$
$$\text{approx\_eq}(a, b) = (\text{diff} \le \text{tol})$$

完整核心逻辑如下：
```c
static cdsl_value_t
builtin_approx_eq(const char* name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
    (void)name;
    cdsl_value_t res = {.type = CDSL_TYPE_BOOL, .data = {.bool_val = 0}};
    if (!args || !args->next || !args->expr || !args->next->expr) {
        return res;
    }
    /* 检查参数数量上限为 3 */
    if (args->next->next && args->next->next->next) {
        return res;
    }

    cdsl_value_t v1 = cdsl_eval_expr_internal(args->expr, ctx, vm, 0, 0);
    cdsl_value_t v2 = cdsl_eval_expr_internal(args->next->expr, ctx, vm, 0, 0);

    /* 校验数值类型并提升至 double */
    double a = 0.0, b = 0.0;
    if (!extract_double_value(&v1, &a) || !extract_double_value(&v2, &b)) {
        cdsl_value_free(&v1);
        cdsl_value_free(&v2);
        return res;
    }
    cdsl_value_free(&v1);
    cdsl_value_free(&v2);

    /* 提取可选的 eps */
    double eps = CDSL_DEFAULT_FLOAT_EPSILON;
    if (args->next->next && args->next->next->expr) {
        cdsl_value_t v3 = cdsl_eval_expr_internal(args->next->next->expr, ctx, vm, 0, 0);
        if (!extract_double_value(&v3, &eps) || eps < 0.0) {
            cdsl_value_free(&v3);
            return res;
        }
        cdsl_value_free(&v3);
    }

    /* 1. IEEE 严格一致优先通过 (覆盖 +0.0 == -0.0 及同号 Inf == Inf) */
    if (a == b) {
        res.data.bool_val = 1;
        return res;
    }

    /* 2. NaN 与任何值均不近似相等 */
    if (isnan(a) || isnan(b)) {
        return res;
    }

    /* 3. 混合绝对容差与相对容差 */
    double diff = fabs(a - b);
    double max_mag = fmax(fabs(a), fabs(b));
    double tol = fmax(eps, eps * max_mag);

    res.data.bool_val = (diff <= tol) ? 1 : 0;
    return res;
}
```

---

### 3.4 运行时文档与注释修正 (`src/vm/eval.c`)

在 [`src/vm/eval.c`](file:///home/quintin/Data/source/c_cpp/cdsl/src/vm/eval.c#L18-L24) 文件头注释中，修正原先关于类型隐式转换和比较的过时描述：
- **修改前**：
  > `Comparison: numerics promoted to double before comparison using fabs(lv - rv) < 1e-9 for EQ.`
- **修改后**：
  > `Comparison: numerics promoted to double before comparison using strict IEEE 754 equality (lv == rv). For fuzzy comparison, use the built-in approx_eq(a, b, [eps]) function.`

---

## 4. 边界处理与安全性设计

| 场景 | 输入样例 | 预期行为 | 设计考量 |
| :--- | :--- | :--- | :--- |
| **标准舍入误差** | `0.1 + 0.2 == 0.3` | `false` | 回归 IEEE 754 严格数学语义 |
| **近似比较成功** | `approx_eq(0.1 + 0.2, 0.3)` | `true` | 双参默认容差 $10^{-9}$ 满足微小舍入 |
| **自定义容差** | `approx_eq(10.0, 10.05, 0.1)` | `true` | 显式传入宽松容差 $0.1$ |
| **自定义容差** | `approx_eq(10.0, 10.05, 0.01)` | `false` | 显式传入严格容差 $0.01$ |
| **大数相对尺度** | `approx_eq(1e12, 1e12 + 100, 1e-9)` | `true` | 相对误差 $10^{-10} \le 10^{-9}$，相对容差生效 |
| **极小数尺度** | `approx_eq(1e-10, 2e-10, 1e-11)` | `false` | 相对误差翻倍，显式严格绝对容差拦截 |
| **跨类型比较** | `approx_eq(5, 5.0000000001)` | `true` | 整型与浮点混合提升 |
| **IEEE NaN** | `approx_eq(0.0/0.0, 0.0/0.0)` | `false` | `NaN` 具备自反不等于特性 |
| **IEEE Inf** | `approx_eq(1.0/0.0, 1.0/0.0)` | `true` | 同号无穷大在原生比较阶段直接命中 |
| **非法容差** | `approx_eq(1.0, 1.0, -0.01)` | `false` | 负容差视为非法输入 |
| **参数数量错误** | `approx_eq(1.0)` / `approx_eq(1,2,3,4)` | `false` | 守卫参数数量合法性 |

---

## 5. 测试矩阵与验证计划

### 5.1 严格等值测试 (`tests/test_execution.c`)
- [ ] 验证 `0.1 + 0.2 == 0.3` 结果为 `false`，`0.1 + 0.2 != 0.3` 结果为 `true`；
- [ ] 验证精确二分浮点数 `3.14 * 2.0 == 6.28` 仍为 `true`；
- [ ] 验证跨类型 `1.0 == 1` 为 `true`，`2.5 == 2` 为 `false`；
- [ ] 验证 IEEE 特殊值：`+0.0 == -0.0` 为 `true`，`NaN == NaN` 为 `false`。

### 5.2 内置函数测试 (`tests/test_builtins.c`)
- [ ] 验证双参 `approx_eq(0.1 + 0.2, 0.3)` 为 `true`；
- [ ] 验证三参自定义容差有效性（包含匹配与不匹配边界）；
- [ ] 验证大数（$10^{12}$ 级别）与极小数尺度下的混合容差判定；
- [ ] 验证 `NaN`、`Inf`、跨类型参数（`INT`、`LONG`、`FLOAT`）的行为；
- [ ] 验证异常输入（缺参、多参、非数值字符串、负容差）安全返回 `false`。

### 5.3 跨后端一致性与回归验证
- [ ] 分别在 **Tree-walk 解释模式** 与 **Bytecode 虚拟机** 下执行上述用例，确保双端输出 100% 同构；
- [ ] 运行全库测试集：`ctest --test-dir build --output-on-failure`，确保全量 29 个既有单元测试与基准测试 100% 通过；
- [ ] 编译检查：保持 `-Wall -Wextra -Werror` 零编译警告。
