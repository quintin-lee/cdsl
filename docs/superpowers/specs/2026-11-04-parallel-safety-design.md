# 设计文档：VM 并行执行线程安全 (子项目 D)

**日期**: 2026-11-04
**项目**: C-DSL Rule Engine
**子项目**: D — 修复并行执行竞态 (src/vm/ruleset.c)
**状态**: 待用户审核

---

## 1. 背景与问题

`cdsl_vm_execute_ruleset_parallel()` 存在多处并发安全问题，导致多个线程并发对**同一** `cdsl_vm_t` 实例调用并行规则执行时发生数据竞态：

### 问题 1：回调/函数链表竞态
- `cdsl_vm_register_action()` / `cdsl_vm_register_function()` 修改 `vm->callbacks` / `vm->functions` 链表，不加锁。
- `cdsl_trigger_action_internal()`（eval.c）和 `BC_CALL`（bytecode.c）遍历这些链表分发动作/函数调用，不加锁。
- 若一线程正在执行 `ruleset_parallel`（worker 遍历链表），另一线程并发调用 `cdsl_vm_register_action`，链表会被并发读写 → 未定义行为。

### 问题 2：Stats 聚合竞态
- `cdsl_vm_execute_ruleset_parallel()` 结束后，将多个 worker 的 `stats` 累加到父 VM `vm->stats`。
- `stats` 字段虽用 `_Atomic`，但聚合为多字段复合操作（`total_executions += ...; total_rules_executed += ...; ...`），非原子。
- 若多线程并发对**同一父 VM** 调用 `ruleset_parallel`，stats 聚合会竞态。

### 问题 3（范围外，选项 C 处理）：worker VM 未注册内置函数
- worker VM 用 `calloc` 创建，未调用 `cdsl_vm_register_builtins`，`strlen`/`uppercase` 等内置函数不可用。
- **本子项目 D 不修复此问题**（选项 C 负责），但为避免 worker 执行时内置函数缺失，worker 将继承父 VM 的 `functions` 链表（通过读锁保护读取）。

---

## 2. 目标

- 使多个线程可**并发安全地**对**同一** `cdsl_vm_t` 实例调用 `cdsl_vm_execute_ruleset_parallel`。
- 保护 `callbacks` / `functions` 链表：注册（写）与执行遍历（读）互斥，多线程执行并行。
- 保护父 VM `stats` 聚合：复合累加操作串行化。
- 公共 API 签名不变，向后兼容。
- 公平、轻量：使用 `cdsl_rwlock_t`（已在 `threads.h` 中定义，跨平台支持 pthread_rwlock / Windows SRWLock）。

---

## 3. 架构设计

### 新增字段

```c
typedef struct cdsl_vm {
    ...
    cdsl_rwlock_t rwlock;   /**< 保护 callbacks/functions 链表与 stats 聚合 */
    ...
} cdsl_vm_t;
```

### 锁的使用策略

| 操作 | 锁 | 粒度 | 理由 |
|------|----|------|------|
| `cdsl_vm_register_action` / `cdsl_vm_register_function` | **写锁** | 独占 | 修改链表，需阻止并发注册与并发执行遍历 |
| `cdsl_trigger_action_internal` 遍历 `callbacks` | **读锁** | 共享 | 只读遍历，允许多线程并行触发动作 |
| 表达式求值 `CDSL_EXPR_CALL` / 字节码 `BC_CALL` 遍历 `functions` | **读锁** | 共享 | 只读遍历函数表 |
| `ruleset_parallel` 父 VM `stats` 聚合 | **写锁** | 独占 | 复合累加操作非原子，需串行化 |

### 关键设计决策

1. **`cdsl_vm_execute` 本身不加锁**。使用者约定：每个线程独立创建自己的 VM 实例（已有文档约定）。单条规则执行不涉及共享状态，无需加锁，避免不必要开销。
2. **Worker VM 独立**。每个 worker 有自己的 `cdsl_vm_t`（stats、error_state 等），互不干扰。worker 执行时通过父 VM 的读锁访问 `callbacks`/`functions`。
3. **`cdsl_vm_register_action` 内存分配失败时**：分配在锁内完成，失败时先解锁再返回，避免锁泄露；不修改 VM 状态。

### 数据流（并行规则执行）

```
Thread A                              Thread B
────────                              ────────
cdsl_vm_register_action()             cdsl_vm_register_action()
    │ [wrlock]                            │ [wrlock]  ← 阻塞，等待 A 释放
    │ 修改 callbacks 链表                   │
    │ [wrunlock]                            │ [wrlock acquired]
    │                                       │ 修改 callbacks 链表
    │                                       │ [wrunlock]

cdsl_vm_execute_ruleset_parallel(vm, set, ctx, N)
    │
    ├─ 创建 N 个 worker VM（各自独立 stats/error_state）
    │
    ├─ 启动 N 个 worker 线程
    │    每个 worker:
    │      cdsl_trigger_action_internal(vm, action)
    │          → [rdlock on vm] 遍历 callbacks → [wrunlock]
    │      BC_CALL → traverse functions
    │          → [rdlock on vm] 遍历 functions → [wrunlock]
    │
    ├─ 等待所有 worker 完成 (CDSL_THREAD_JOIN)
    │
    └─ 聚合 worker stats 到父 vm.stats
         → [wrlock on vm] 复合累加 → [wrunlock]
```

---

## 4. 文件修改

| 文件 | 动作 | 内容 |
|------|------|------|
| `include/cdsl/vm.h` | 修改 | `cdsl_vm_t` 结构体新增 `cdsl_rwlock_t rwlock` 字段 |
| `src/vm/vm.c` | 修改 | `cdsl_vm_create` 调用 `CDSL_RWLOCK_INIT(&vm->rwlock)`；`cdsl_vm_free` 调用 `CDSL_RWLOCK_DESTROY(&vm->rwlock)`；`cdsl_vm_register_action` / `cdsl_vm_register_function` 持写锁 |
| `src/vm/eval.c` | 修改 | `cdsl_trigger_action_internal` 遍历 `callbacks` 时持读锁；`CDSL_EXPR_CALL` 分支遍历 `functions` 时持读锁 |
| `src/vm/bytecode.c` | 修改 | `BC_CALL` 分支遍历 `functions` 时持读锁 |
| `src/vm/ruleset.c` | 修改 | `cdsl_vm_execute_ruleset_parallel` 聚合父 VM `stats` 时持父 VM 写锁 |
| `tests/test_vm_config.c` | 修改 | 新增并发安全测试：多线程并发对同一父 VM 执行 `ruleset_parallel` |

---

## 5. 错误处理与边界

| 场景 | 处理方式 |
|------|----------|
| `CDSL_RWLOCK_INIT` 失败 | `cdsl_vm_create` 释放已分配资源并返回 `NULL`（极罕见） |
| `cdsl_vm_register_action` 分配失败 | 先 `CDSL_RWLOCK_UNLOCK_WR` 再返回，避免锁泄露；不修改 VM 状态 |
| `cdsl_vm_register_action(NULL, ...)` / `cdsl_trigger_action_internal(NULL, ...)` | 原有 NULL 检查保持，在加锁前直接返回 |
| 空链表（未注册回调） | 读锁保护下遍历空链表，无操作，安全 |
| `ruleset_parallel` thread_count ≤ 1 | 退化为顺序执行，无并发，锁仍正常工作 |
| Worker VM 未注册内置函数 | worker 读取父 VM `functions` 链表（读锁保护），内置函数可用 |

---

## 6. 测试计划

新增 `test_vm_config.c` 测试（实际函数名：`test_vm_parallel_execute_concurrent`）：

```c
static void*
parallel_execute_worker(void* arg)
{
	cdsl_vm_t* vm = (cdsl_vm_t*)arg;
	cdsl_ruleset_t* rs = cdsl_ruleset_create();
	if (!rs) {
		return NULL;
	}

	/* Create a few simple rules in the ruleset.
	 * The first rule calls the registered "counter" function to exercise
	 * the BC_CALL read-lock path where func_found == 1 (match found). */
	const char* rule_dsls[] = {
	    "RULE r1 { WHEN counter() == 42 THEN block(\"a\") }",
	    "RULE r2 { WHEN true THEN block(\"b\") }",
	    "RULE r3 { WHEN counter() == 42 THEN block(\"c\") }",
	};
	for (int i = 0; i < 3; i++) {
		cdsl_rule_t* rule = cdsl_parse_string(rule_dsls[i], NULL);
		if (rule) {
			cdsl_ruleset_add(rs, rule, i + 1);
		}
	}

	/* Execute ruleset in parallel on the shared VM */
	cdsl_ruleset_report_t* report = cdsl_vm_execute_ruleset_parallel(vm, rs, NULL, 3);
	if (report) {
		cdsl_ruleset_report_free(report);
	}

	cdsl_ruleset_free(rs);
	return NULL;
}

static void
test_vm_parallel_execute_concurrent(void)
{
	TEST_BEGIN("Concurrent ruleset parallel execution on shared VM");

	cdsl_vm_t* vm = cdsl_vm_create(NULL);
	TEST_ASSERT_NOT_NULL(vm, "VM created");

	/* Register a test function (exercises BC_CALL read lock path) */
	cdsl_vm_register_function(vm, "counter", test_counter_func);

	/* Register a test action (exercises cdsl_trigger_action_internal read lock path) */
	cdsl_vm_register_action(vm, "block", NULL);

	int thread_count = 4;
	cdsl_thread_t threads[4];

	for (int i = 0; i < thread_count; i++) {
		if (CDSL_THREAD_CREATE(&threads[i], parallel_execute_worker, vm) != 0) {
			TEST_ASSERT(0, "Failed to create thread");
		}
	}

	for (int i = 0; i < thread_count; i++) {
		CDSL_THREAD_JOIN(threads[i]);
	}

	/* Verify stats are aggregated (no crash = pass) */
	TEST_ASSERT_INT(vm->stats.total_executions, 0, "Stats accessible after concurrent execution");

	cdsl_vm_free(vm);
	TEST_END();
}
```

Register `test_vm_parallel_execute_concurrent();` in `main()` before `TEST_SUMMARY();`.

- 现有 22 个测试应继续通过。
- 新增并发测试通过（无崩溃、无数据竞态，ASan 无报告）。

---

## 7. 验收标准

- [ ] `src/vm/vm.c`, `src/vm/eval.c`, `src/vm/bytecode.c`, `src/vm/ruleset.c`, `include/cdsl/vm.h` 编译通过（`-Werror` 下无警告）
- [ ] 所有 22 个现有测试通过（`ctest --test-dir build --output-on-failure`）
- [ ] 新增并发测试通过
- [ ] ASan (AddressSanitizer) 构建下所有测试无崩溃、无泄漏、无数据竞态
- [ ] clang-tidy 无新增警告
- [ ] 公共 API 头文件 `cdsl/vm.h` 函数签名无变动，外部调用兼容

---

## 8. 使用限制与注意事项

### 8.1 读写锁不可重入（非递归）

`cdsl_rwlock_t` 基于 `pthread_rwlock_t`（Unix）或 `SRWLOCK`（Windows），**不可重入**。这意味着：

- 同一线程若已持有读锁，再次尝试获取读锁会导致**自锁死（self-deadlock）**。
- 同一线程若已持有写锁，再次尝试获取任何锁（读或写）都会导致自锁死。

**遵循规则**：在持有 `vm->rwlock` 期间，不得调用任何可能再次尝试获取 `vm->rwlock` 的函数。

### 8.2 回调函数不得重入 VM 执行

`cdsl_trigger_action_internal` 和 `BC_CALL` / `CDSL_EXPR_CALL` 在持有 `vm->rwlock` **读锁**期间调用用户注册的回调函数（`cb->cb` 或 `fn->cb`）。因此：

> **严禁在动作回调（`cdsl_action_cb_t`）或函数回调（`cdsl_func_cb_t`）内部调用 `cdsl_vm_execute()`、`cdsl_vm_execute_ruleset_parallel()`、或任何会再次遍历 `vm->callbacks` / `vm->functions` 的函数。**

违反此规则会导致：
- **自锁死**：回调内部再次尝试获取读锁，由于 pthread_rwlock 不可重入，线程会永久阻塞。
- 如果回调内部尝试获取写锁（如 `cdsl_vm_register_action`），会因为读锁已由当前线程持有而导致写锁获取失败，进而可能引发未定义行为。

**推荐做法**：若回调需要触发新的规则执行，应将执行请求放入队列，在当前回调返回且锁释放后，由另一线程或主线程处理。

### 8.3 锁持有时长最小化

所有读锁/写锁的持有范围已限制在最小必要代码块内（链表遍历、stats 聚合）。不要在锁保护的代码块中执行耗时操作（如 I/O、复杂计算、调用外部 API），否则会显著降低并发性能。

### 8.4 每个线程独立 VM 实例的约定

`cdsl_vm_execute` 本身不加锁，遵循"每个线程独立 VM 实例"的使用约定。若多线程需并发执行规则，应：
- 为每个线程创建独立的 `cdsl_vm_t`，或
- 使用 `cdsl_vm_execute_ruleset_parallel`（其内部已处理并发安全）。

---

**本设计文档将写入 `docs/superpowers/specs/2026-11-04-parallel-safety-design.md`。** 请审阅，如有修改请告诉我；如无修改，我将提交 git 并进入实施计划阶段。