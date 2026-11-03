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

新增 `test_vm_config.c` 测试：

```c
static void
test_vm_parallel_concurrent_safe(void)
{
    TEST_BEGIN("VM parallel concurrent safety");
    cdsl_schema_t* schema = cdsl_schema_create();
    cdsl_vm_t* vm = cdsl_vm_create(schema);
    cdsl_context_t* ctx = cdsl_context_create(schema);

    /* 注册一个动作 */
    cdsl_vm_register_action(vm, "block", test_action_cb);

    /* 构造 ruleset：多条规则 */
    cdsl_ruleset_t* set = cdsl_ruleset_create();
    ... (添加 3 条简单规则) ...

    /* 多线程并发对同一父 VM 执行 ruleset_parallel */
    #define NTHREADS 4
    cdsl_ruleset_report_t* results[NTHREADS];
    cdsl_thread_t threads[NTHREADS];
    for (int i = 0; i < NTHREADS; i++) {
        CDSL_THREAD_CREATE(&threads[i], parallel_runner, &results[i]);
    }
    for (int i = 0; i < NTHREADS; i++) {
        CDSL_THREAD_JOIN(threads[i]);
        TEST_ASSERT_NOT_NULL(results[i], "parallel result not NULL");
        cdsl_ruleset_report_free(results[i]);
    }

    cdsl_ruleset_free(set);
    cdsl_context_free(ctx);
    cdsl_vm_free(vm);
    cdsl_schema_free(schema);
    TEST_END();
}
```

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

**本设计文档将写入 `docs/superpowers/specs/2026-11-04-parallel-safety-design.md`。** 请审阅，如有修改请告诉我；如无修改，我将提交 git 并进入实施计划阶段。