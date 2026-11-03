# 设计文档：vm.c 模块整合 (子项目 A)

**日期**: 2026-11-03
**项目**: C-DSL Rule Engine
**子项目**: A — 创建 `src/vm/vm.c` 并整合 VM 生命周期函数
**状态**: 待用户审核

---

## 1. 背景与问题

目前 C-DSR 的 VM 相关函数分散在 `src/vm/eval.c` 中，与表达式求值、规则执行逻辑混在一起。`src/vm/vm.c` 不存在，导致 VM 的生命周期管理、配置、统计、沙箱配额等职责与核心执行引擎耦合，模块边界不清晰。

## 2. 目标

- 新建 `src/vm/vm.c`，承载除 `cdsl_vm_execute` 以外的所有 VM 生命周期、配置、统计、沙箱配额、跟踪函数。
- `src/vm/eval.c` 保留 `cdsl_vm_execute`、表达式求值、动作触发、报告生成等核心执行逻辑。
- 保持现有公共 API（`cdsl/vm.h`）不变，不对外部调用者造成破坏性改动。
- 保持 NULL 安全、线程安全、资源无泄漏。

## 3. 文件结构与依赖

### 新建 `src/vm/vm.c`

包含头文件：
- `cdsl/execution.h`（公开 API，间接包含 `cdsl/vm.h`）
- `internal.h`（内部符号：`cdsl_vm_register_builtins`）
- `stdlib.h`、`string.h`

职责：
- `cdsl_vm_create`：分配 `cdsl_vm_t`，注册内置函数，返回新 VM
- `cdsl_vm_free`：释放动作回调链表、自定义函数链表、VM 结构体
- `cdsl_vm_register_action` / `cdsl_vm_register_function`：注册回调/函数
- `cdsl_vm_set_debug` / `cdsl_vm_get_max_expr_depth` / `cdsl_vm_set_max_expr_depth`
- `cdsl_vm_get_stats` / `cdsl_vm_reset_stats`
- `cdsl_vm_set_timeout/get` / `cdsl_vm_set_memory_limit/get` / `cdsl_vm_set_instruction_limit/get`
- `cdsl_vm_set_trace_callback`

### 调整 `src/vm/eval.c`

移除上述已迁移函数的定义，保留：
- `cdsl_vm_execute`（规则执行入口）
- `cdsl_vm_execute_compiled`（字节码规则执行）
- `cdsl_eval_expr_internal`（树走行表达式求值）
- `cdsl_trigger_action_internal`（动作回调分发）
- `execute_metric_rule` / `execute_simple_rule`（内部规则执行）
- `evaluate_condition` / `fire_trace`
- `cdsl_status_str_internal`
- `cdsl_report_free` / `cdsl_report_print` / `cdsl_report_to_json`

### `internal.h`

保持不变，继续声明 `cdsl_vm_register_builtins`、`cdsl_vm_track_alloc/free`、`cdsl_vm_check_abort` 等内部符号。

## 4. 函数迁移清单

从 `eval.c` → `vm.c`（共 16 个函数）：

| 函数 | 说明 |
|------|------|
| `cdsl_vm_create` | VM 创建，内部调用 `cdsl_vm_register_builtins` |
| `cdsl_vm_free` | VM 销毁，释放回调/函数链表 |
| `cdsl_vm_register_action` | 注册动作回调 |
| `cdsl_vm_register_function` | 注册自定义函数 |
| `cdsl_vm_set_debug` | 开启/关闭 stderr 调试输出 |
| `cdsl_vm_get_max_expr_depth` | 获取表达式深度限制 |
| `cdsl_vm_set_max_expr_depth` | 设置表达式深度限制 |
| `cdsl_vm_get_stats` | 获取执行统计快照 |
| `cdsl_vm_reset_stats` | 重置统计计数器 |
| `cdsl_vm_set_timeout` | 设置执行超时 |
| `cdsl_vm_get_timeout` | 获取超时配置 |
| `cdsl_vm_set_memory_limit` | 设置内存限制 |
| `cdsl_vm_get_memory_limit` | 获取内存限制 |
| `cdsl_vm_set_instruction_limit` | 设置指令配额 |
| `cdsl_vm_get_instruction_limit` | 获取指令配额 |
| `cdsl_vm_set_trace_callback` | 设置执行跟踪回调 |

保留在 `eval.c`（不动）：`cdsl_vm_execute`、`cdsl_vm_execute_compiled`、`cdsl_eval_expr_internal`、`cdsl_trigger_action_internal`、报告相关函数等。

## 5. 数据流

```
用户代码
   │
   ▼
cdsl_vm_create() ─────────► vm.c：分配 VM，注册内置函数，返回 cdsl_vm_t*
   │
   ▼
cdsl_vm_register_action() ──► vm.c：向 callbacks 链表追加条目
cdsl_vm_register_function() ──► vm.c：向 functions 链表追加条目
cdsl_vm_set_timeout/memory/instruction ───► vm.c：设置沙箱配额字段
cdsl_vm_set_trace_callback() ─────────────► vm.c：设置跟踪回调
   │
   ▼
cdsl_vm_execute() ─────────► eval.c：重置 error_state，调用 execute_metric_rule/execute_simple_rule
   │                                                       │
   │                                                       ▼
   │                                               cdsl_eval_expr_internal()（树走行求值）
   │                                                       │
   │                                                       ▼
   │                                               cdsl_trigger_action_internal()（触发动作回调）
   │                                                       │
   │                                                       ▼
   │                                               cdsl_report_free/print/to_json（报告生成）
   │
   ▼
cdsl_vm_free() ───────────► vm.c：释放 callbacks/functions 链表和 VM 结构体
```

## 6. 错误处理与资源安全

### 资源所有权
- `cdsl_vm_create`：分配 `cdsl_vm_t`，内部调用 `cdsl_vm_register_builtins`；分配失败返回 `NULL`，无泄漏。
- `cdsl_vm_free`：NULL 安全；遍历释放 `callbacks`（每个节点的 `action_name` 和节点本身）、`functions`（每个节点的 `func_name` 和节点本身），最后释放 VM。
- `cdsl_vm_register_action` / `cdsl_vm_register_function`：内部 `strdup` 名称字符串；分配失败时不修改 VM 状态，无副作用。

### 沙箱配额与错误状态
- `error_state`（`cdsl_atomic_int_t`）在 `cdsl_vm_execute` 开始时重置为 0。
- 超时、内存超限、指令配额超限时由 `cdsl_vm_check_abort` 或指令计数检查置位 `error_state`。
- 执行结束若 `error_state` 非零，`cdsl_vm_execute` 返回 `CDSL_STATUS_ERROR` 报告。

## 7. 线程安全

- 每个线程独立创建自己的 `cdsl_vm_t` 实例（已有文档约定）。
- `stats` 使用 `_Atomic` 类型，支持无锁读取。
- 回调链表和函数链表在 VM 创建后、执行期间不应被并发修改（使用者责任）。

## 8. 测试计划

- 现有 `tests/test_vm_config.c` 应继续通过，覆盖 VM 配置函数。
- 新增或扩展测试覆盖：
  - `cdsl_vm_create` 返回非 NULL 且内置函数已注册（可通过 `cdsl_vm_get_stats` 验证 VM 可用）。
  - `cdsl_vm_free(NULL)` 不崩溃。
  - 注册重复动作/函数名称不导致崩溃。
  - 沙箱配额设置/获取一致性（设置后可读回相同值）。
  - `cdsl_vm_reset_stats` 后计数器归零。

## 9. 验收标准

- [ ] `src/vm/vm.c` 存在并编译通过（无警告，`-Werror` 下）。
- [ ] `src/vm/eval.c` 不再定义已迁移的 16 个函数。
- [ ] 所有现有单元测试通过（`ctest --test-dir build --output-on-failure`）。
- [ ] 公共 API 头文件 `cdsl/vm.h` 无变动，外部调用兼容。
- [ ] clang-tidy 静态分析无新增警告。

---

**本设计文档已写入 `docs/superpowers/specs/2026-11-03-vm-module-design.md`。** 请审阅，如有修改请告诉我；如无修改，我将提交 git 并进入实施计划阶段（调用 writing-plans 技能）。