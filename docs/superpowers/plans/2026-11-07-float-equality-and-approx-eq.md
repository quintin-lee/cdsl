# 浮点数严格等值比较回归与 approx_eq 内置函数实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 消除 C-DSL 核心求值层中浮点比较的硬编码绝对容差（`1e-9`），使算子 `==` / `!=` 回归 IEEE 754 严格等值（与 Codegen 100% 对齐），并新增支持混合容差的 `approx_eq(a, b, [eps])` 内置函数。

**Architecture:** 将 `src/vm/value_ops.c` 中的浮点 `CDSL_OP_EQ` / `CDSL_OP_NE` 还原为 C 原生 `av == bv` 与 `av != bv`；在 `include/cdsl/vm.h` 定义公共宏 `CDSL_DEFAULT_FLOAT_EPSILON`；在 `src/vm/builtins.c` 中实现综合绝对容差与相对容差的 `builtin_approx_eq`，并注册至虚拟机；在 `tests/` 中补齐精度、边界、IEEE 特殊值与跨引擎差分测试。

**Tech Stack:** C99, CMake, Ninja, CTest

---

## 文件结构变更规划

- **修改**：
  - `include/cdsl/vm.h`：添加 `CDSL_DEFAULT_FLOAT_EPSILON` 宏定义（`1e-9`）。
  - `src/vm/value_ops.c`：将浮点分支中 `CDSL_OP_EQ` / `CDSL_OP_NE` 替换为严格等值比较。
  - `src/vm/eval.c`：更新文件头部关于浮点比较机制的文档注释。
  - `src/vm/builtins.c`：实现 `builtin_approx_eq` 并在 `cdsl_vm_register_builtins` 中注册。
- **测试**：
  - `tests/test_execution.c`：增加浮点严格等值断言用例（验证 `0.1 + 0.2 == 0.3` 为 `false` 等）。
  - `tests/test_builtins.c`：增加 `approx_eq` 完整用例集（双参、三参、大小数、NaN/Inf、错误处理）。
  - `tests/test_differential.c`：增加 AST 解释器与 Bytecode VM 跨后端同构对比测试。

---

### Task 1: 核心运算符严格等值回归与执行层文档同步

**Files:**
- Modify: `src/vm/value_ops.c:255-265`
- Modify: `src/vm/eval.c:18-28`
- Test: `tests/test_execution.c`

- [ ] **Step 1: 在 `tests/test_execution.c` 中编写浮点严格等值测试**

在 `tests/test_execution.c` 末尾添加测试函数 `test_float_strict_equality`：

```c
static void
test_float_strict_equality(void)
{
	TEST_BEGIN("Float strict IEEE 754 equality");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	/* 1. IEEE 754 典型舍入误差: 0.1 + 0.2 不等于 0.3 */
	cdsl_rule_t* r_fail = cdsl_parse_string(
	    "RULE r_fail { WHEN 0.1 + 0.2 == 0.3 THEN block(\"should_not_fire\") }", NULL);
	TEST_ASSERT_NOT_NULL(r_fail, "parse 0.1 + 0.2 == 0.3 ok");
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, r_fail, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute ok");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "0.1 + 0.2 == 0.3 condition should be false -> PASSED");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_fail);

	/* 2. 0.1 + 0.2 != 0.3 为 true -> 触发 block -> FAILED 状态 */
	cdsl_rule_t* r_ne = cdsl_parse_string(
	    "RULE r_ne { WHEN 0.1 + 0.2 != 0.3 THEN block(\"not_equal\") }", NULL);
	TEST_ASSERT_NOT_NULL(r_ne, "parse 0.1 + 0.2 != 0.3 ok");
	rpt = cdsl_vm_execute(vm, r_ne, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute ok");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "0.1 + 0.2 != 0.3 condition is true -> FAILED");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_ne);

	/* 3. 精确二分浮点等值 3.14 == 3.14 */
	cdsl_rule_t* r_exact = cdsl_parse_string(
	    "RULE r_exact { WHEN 3.14 == 3.14 THEN block(\"exact\") }", NULL);
	TEST_ASSERT_NOT_NULL(r_exact, "parse 3.14 == 3.14 ok");
	rpt = cdsl_vm_execute(vm, r_exact, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute ok");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "3.14 == 3.14 condition is true -> FAILED");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_exact);

	/* 4. 浮点与整型跨类型严格等值 1.0 == 1 */
	cdsl_rule_t* r_mixed = cdsl_parse_string(
	    "RULE r_mixed { WHEN 1.0 == 1 THEN block(\"mixed\") }", NULL);
	TEST_ASSERT_NOT_NULL(r_mixed, "parse 1.0 == 1 ok");
	rpt = cdsl_vm_execute(vm, r_mixed, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute ok");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "1.0 == 1 condition is true -> FAILED");
	cdsl_report_free(rpt);
	cdsl_free_rule(r_mixed);

	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}
```

并在 `tests/test_execution.c` 的 `main()` 函数中调用 `test_float_strict_equality();`。

- [ ] **Step 2: 编译并运行测试，验证其因旧硬编码容差而失败**

运行：
```bash
cmake --build build --target test_execution && ./build/tests/test_execution
```
预期输出：测试在 `0.1 + 0.2 == 0.3` 处失败（因为旧代码中 `fabs(0.30000000000000004 - 0.3) < 1e-9` 误判为 true）。

- [ ] **Step 3: 修改 `src/vm/value_ops.c` 与 `src/vm/eval.c`**

在 `src/vm/value_ops.c` 中，将：
```c
		if (is_comparison_op(op)) {
			out->type = CDSL_TYPE_BOOL;
			out->data.bool_val = (op == CDSL_OP_EQ)	  ? (fabs(av - bv) < 1e-9)
					     : (op == CDSL_OP_NE) ? (fabs(av - bv) >= 1e-9)
					     : (op == CDSL_OP_LT) ? (av < bv)
					     : (op == CDSL_OP_GT) ? (av > bv)
					     : (op == CDSL_OP_LE) ? (av <= bv)
								  : (av >= bv);
			return CDSL_VALOP_OK;
		}
```
修改为：
```c
		if (is_comparison_op(op)) {
			out->type = CDSL_TYPE_BOOL;
			out->data.bool_val = (op == CDSL_OP_EQ)	  ? (av == bv)
					     : (op == CDSL_OP_NE) ? (av != bv)
					     : (op == CDSL_OP_LT) ? (av < bv)
					     : (op == CDSL_OP_GT) ? (av > bv)
					     : (op == CDSL_OP_LE) ? (av <= bv)
								  : (av >= bv);
			return CDSL_VALOP_OK;
		}
```

同时，更新 `src/vm/eval.c` 第 18-24 行注释：
```c
 * ### Type coercion in binary expressions
 *
 * Comparison: numerics promoted to double before comparison using
 * strict IEEE 754 equality (lv == rv).  For fuzzy comparison, use
 * the built-in approx_eq(a, b, [eps]) function.  Strings via strcmp(),
 * dates via time_t arithmetic.
```

- [ ] **Step 4: 编译并运行测试验证通过**

运行：
```bash
cmake --build build --target test_execution && ./build/tests/test_execution
```
预期输出：`Float strict IEEE 754 equality` 全部通过，所有断言成功。

- [ ] **Step 5: 提交改动**

```bash
git add src/vm/value_ops.c src/vm/eval.c tests/test_execution.c
git commit -m "refactor(eval): ⚡ switch float equality to strict IEEE 754 semantics"
```

---

### Task 2: 公共容差宏与 `approx_eq` 内置函数实现与测试

**Files:**
- Modify: `include/cdsl/vm.h:20-30`
- Modify: `src/vm/builtins.c`
- Test: `tests/test_builtins.c`

- [ ] **Step 1: 在 `tests/test_builtins.c` 中编写 `approx_eq` 测试用例**

在 `tests/test_builtins.c` 中添加测试函数 `test_builtin_approx_eq`：

```c
static void
test_builtin_approx_eq(void)
{
	TEST_BEGIN("built-in approx_eq");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);

	/* 1. 双参默认容差: 0.1 + 0.2 近似等于 0.3 */
	cdsl_rule_t* r1 = cdsl_parse_string(
	    "RULE r1 { WHEN approx_eq(0.1 + 0.2, 0.3) THEN block(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(r1, "parse r1 ok");
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, r1, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute r1 ok");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "approx_eq(0.1+0.2, 0.3) is true -> FAILED");
	cdsl_report_free(rpt);
	cdsl_free_rule(r1);

	/* 2. 三参自定义容差: 宽松匹配成功 (10.0 与 10.05, 容差 0.1) */
	cdsl_rule_t* r2 = cdsl_parse_string(
	    "RULE r2 { WHEN approx_eq(10.0, 10.05, 0.1) THEN block(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(r2, "parse r2 ok");
	rpt = cdsl_vm_execute(vm, r2, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute r2 ok");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "approx_eq with eps=0.1 matches");
	cdsl_report_free(rpt);
	cdsl_free_rule(r2);

	/* 3. 三参自定义容差: 严格匹配失败 (10.0 与 10.05, 容差 0.01) */
	cdsl_rule_t* r3 = cdsl_parse_string(
	    "RULE r3 { WHEN approx_eq(10.0, 10.05, 0.01) THEN block(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(r3, "parse r3 ok");
	rpt = cdsl_vm_execute(vm, r3, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute r3 ok");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "approx_eq with eps=0.01 does not match -> PASSED");
	cdsl_report_free(rpt);
	cdsl_free_rule(r3);

	/* 4. 大数相对尺度匹配 (1e12 与 1e12 + 100, eps 1e-9) */
	cdsl_rule_t* r4 = cdsl_parse_string(
	    "RULE r4 { WHEN approx_eq(1000000000000.0, 1000000000100.0, 0.000000001) THEN block(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(r4, "parse r4 ok");
	rpt = cdsl_vm_execute(vm, r4, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute r4 ok");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "large number relative approx_eq matches");
	cdsl_report_free(rpt);
	cdsl_free_rule(r4);

	/* 5. 跨类型参数 (整型与浮点提升) */
	cdsl_rule_t* r5 = cdsl_parse_string(
	    "RULE r5 { WHEN approx_eq(5, 5.0000000001) THEN block(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(r5, "parse r5 ok");
	rpt = cdsl_vm_execute(vm, r5, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute r5 ok");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_FAILED, "mixed int/float approx_eq matches");
	cdsl_report_free(rpt);
	cdsl_free_rule(r5);

	/* 6. 异常参数防护: 负容差视为非法返回 false */
	cdsl_rule_t* r6 = cdsl_parse_string(
	    "RULE r6 { WHEN approx_eq(1.0, 1.0, -0.01) THEN block(\"ok\") }", NULL);
	TEST_ASSERT_NOT_NULL(r6, "parse r6 ok");
	rpt = cdsl_vm_execute(vm, r6, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "execute r6 ok");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_PASSED, "negative eps returns false -> PASSED");
	cdsl_report_free(rpt);
	cdsl_free_rule(r6);

	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}
```

并在 `tests/test_builtins.c` 的 `main()` 中注册并调用 `test_builtin_approx_eq();`。

- [ ] **Step 2: 编译并运行测试，验证其因函数未注册而失败**

运行：
```bash
cmake --build build --target test_builtins && ./build/tests/test_builtins
```
预期输出：测试失败，提示 `Unknown function 'approx_eq'`。

- [ ] **Step 3: 在 `include/cdsl/vm.h` 中定义公共容差常量**

在 `include/cdsl/vm.h` 的常量定义区域追加：
```c
/** Default epsilon for approximate floating-point equality (1e-9) */
#define CDSL_DEFAULT_FLOAT_EPSILON 1e-9
```

- [ ] **Step 4: 在 `src/vm/builtins.c` 中实现并注册 `builtin_approx_eq`**

在 `src/vm/builtins.c` 中编写辅助数值解包函数与 `builtin_approx_eq`：

```c
static int
value_to_double(const cdsl_value_t* v, double* out)
{
	if (!v || !out) {
		return 0;
	}
	switch (v->type) {
	case CDSL_TYPE_FLOAT:
		*out = v->data.float_val;
		return 1;
	case CDSL_TYPE_INT:
		*out = (double)v->data.int_val;
		return 1;
	case CDSL_TYPE_LONG:
		*out = (double)v->data.long_val;
		return 1;
	case CDSL_TYPE_BOOL:
		*out = (double)v->data.bool_val;
		return 1;
	default:
		return 0;
	}
}

/**
 * @brief built-in approx_eq(a, b, [eps]): checks if two numbers are approximately equal.
 *
 * Tolerance formula: diff <= fmax(eps, eps * fmax(|a|, |b|))
 */
static cdsl_value_t
builtin_approx_eq(const char* name, cdsl_arg_node_t* args, cdsl_context_t* ctx, cdsl_vm_t* vm)
{
	(void)name;
	cdsl_value_t res = {.type = CDSL_TYPE_BOOL, .data = {.bool_val = 0}};
	if (!args || !args->next || !args->expr || !args->next->expr) {
		return res;
	}
	if (args->next->next && args->next->next->next) {
		/* More than 3 arguments -> invalid */
		return res;
	}

	cdsl_value_t v1 = cdsl_eval_expr_internal(args->expr, ctx, vm, 0, 0);
	cdsl_value_t v2 = cdsl_eval_expr_internal(args->next->expr, ctx, vm, 0, 0);

	double a = 0.0, b = 0.0;
	if (!value_to_double(&v1, &a) || !value_to_double(&v2, &b)) {
		cdsl_value_free(&v1);
		cdsl_value_free(&v2);
		return res;
	}
	cdsl_value_free(&v1);
	cdsl_value_free(&v2);

	double eps = CDSL_DEFAULT_FLOAT_EPSILON;
	if (args->next->next && args->next->next->expr) {
		cdsl_value_t v3 = cdsl_eval_expr_internal(args->next->next->expr, ctx, vm, 0, 0);
		if (!value_to_double(&v3, &eps) || eps < 0.0) {
			cdsl_value_free(&v3);
			return res;
		}
		cdsl_value_free(&v3);
	}

	/* Exact equality covers +0.0 == -0.0 and identical infinities */
	if (a == b) {
		res.data.bool_val = 1;
		return res;
	}
	if (isnan(a) || isnan(b)) {
		return res;
	}

	double diff = fabs(a - b);
	double max_mag = fmax(fabs(a), fabs(b));
	double tol = fmax(eps, eps * max_mag);

	res.data.bool_val = (diff <= tol) ? 1 : 0;
	return res;
}
```

并在 `cdsl_vm_register_builtins(cdsl_vm_t* vm)` 中添加注册：
```c
cdsl_vm_register_function(vm, "approx_eq", builtin_approx_eq);
```

- [ ] **Step 5: 编译并运行测试，验证通过**

运行：
```bash
cmake --build build --target test_builtins && ./build/tests/test_builtins
```
预期输出：`built-in approx_eq` 测试通过。

- [ ] **Step 6: 提交改动**

```bash
git add include/cdsl/vm.h src/vm/builtins.c tests/test_builtins.c
git commit -m "feat(vm): ✨ add approx_eq builtin function with hybrid tolerance"
```

---

### Task 3: 跨引擎同构验证与全量回归测试

**Files:**
- Test: `tests/test_differential.c`

- [ ] **Step 1: 在 `tests/test_differential.c` 中添加浮点等值差分用例**

在 `tests/test_differential.c` 的 `run_differential_tests` 中添加差分验证：
```c
	/* Float strict equality differential tests */
	run_diff_test("diff: float strict equality false",
		      "RULE flt_eq { WHEN 0.1 + 0.2 == 0.3 THEN notify(\"eq\") }",
		      schema,
		      ctx,
		      0);

	run_diff_test("diff: float approx_eq true",
		      "RULE flt_approx { WHEN approx_eq(0.1 + 0.2, 0.3) THEN notify(\"approx\") }",
		      schema,
		      ctx,
		      1);
```

- [ ] **Step 2: 编译并运行差分测试**

运行：
```bash
cmake --build build --target test_differential && ./build/tests/test_differential
```
预期输出：差分测试通过，Tree-walk 与 Bytecode 执行结果 100% 同构一致。

- [ ] **Step 3: 运行全量测试套件并检查零回归**

运行：
```bash
ctest --test-dir build --output-on-failure
```
预期输出：100% tests passed, 0 failures (全部 29 个测试及新增用例全部通过)。

- [ ] **Step 4: 提交改动**

```bash
git add tests/test_differential.c
git commit -m "test(differential): ✅ add cross-engine verification for strict float eq and approx_eq"
```
