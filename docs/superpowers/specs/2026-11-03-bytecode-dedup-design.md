# 设计文档：字节码执行器去重 (子项目 B)

**日期**: 2026-11-03
**项目**: C-DSL Rule Engine
**子项目**: B — 字节码执行器去重 (`src/vm/bytecode.c`)
**状态**: 待用户审核

---

## 1. 背景与问题

`src/vm/bytecode.c` 中有两个字节码执行器：

- `cdsl_bytecode_execute()`：完整的栈式 VM 执行循环，支持全部类型（int/float/long/bool/string/date/array）的算术、比较、调用、跳转等指令。
- `cdsl_bytecode_execute_rule()`：重复实现了普通指令（PUSH/POP/算术/比较/跳转等），但**是简化版**——只处理 `int` 类型的算术和比较，不支持 float/long/bool/string/date 等。此外它自己处理 metric 指令（`BC_METRIC_START`/`BC_SET_SCORE`/`BC_FAIL_METRIC`/`BC_RULE_END`）。

这导致：
1. **代码重复**：约 150 行普通指令处理代码被复制两份。
2. **行为不一致**：`cdsl_bytecode_execute_rule` 的简化版算术/比较在遇到非 int 类型时会产生错误结果，而 `cdsl_bytecode_execute` 处理正确。
3. **维护风险**：普通指令逻辑的任何修改都需要同步更新两处，容易遗漏。

## 2. 目标

- 提取共享的核心指令执行循环为内部函数 `bc_run_instructions()`。
- `cdsl_bytecode_execute` 委托给 `bc_run_instructions()`，传 `metric_handler = NULL`（metric 指令被跳过），行为与当前完全一致。
- `cdsl_bytecode_execute_rule` 委托普通指令给 `bc_run_instructions()`，只自己处理 metric 指令，获得完整类型支持，修复类型处理缺陷。
- 公共 API 签名不变，向后兼容。

## 3. 内部函数设计

### `bc_metric_handler_t` 回调类型

```c
typedef int (*bc_metric_handler_t)(bc_op_t op, const cdsl_bytecode_t* bc,
                                   const bc_inst_t** ip, void* user_data);
```

- 当 `bc_run_instructions()` 遇到 metric 指令且 `metric_handler != NULL` 时调用。
- 返回 1 表示已处理；返回 0 表示未处理，调用方执行 `ip++`。

### `bc_run_instructions()` 核心循环

```c
int
bc_run_instructions(cdsl_vm_t* vm, const cdsl_bytecode_t* bc, cdsl_context_t* ctx,
                    bc_metric_handler_t metric_handler, void* user_data,
                    cdsl_value_t* out_result);
```

职责：
- 运行指令循环（stack machine），包括沙箱配额检查（指令配额、超时/OOM）。
- 普通 opcode：直接执行（PUSH/POP/算术/比较/调用/跳转/不等等，与当前 `cdsl_bytecode_execute` 逻辑一致）。
- Metric opcode：若 `metric_handler != NULL` 调用回调；否则 `ip++`（跳过，当前 `cdsl_bytecode_execute` 的行为）。
- `out_result`：仅在 `metric_handler == NULL` 时有意义，返回栈顶值。

## 4. 文件修改

| 文件 | 动作 | 内容 |
|------|------|------|
| `src/vm/internal.h` | 修改 | 新增 `bc_metric_handler_t` 类型和 `bc_run_instructions()` 声明 |
| `src/vm/bytecode.c` | 修改 | 提取核心循环到 `bc_run_instructions()`；重构 `cdsl_bytecode_execute` 和 `cdsl_bytecode_execute_rule` |
| `tests/test_bytecode.c` | 新建/修改 | 新增测试：metric 规则字节码执行（得分、违规原因、三态）；普通表达式规则字节码执行（float/string/date 比较） |

## 5. 重构前后对比

### 重构前

```
cdsl_bytecode_execute:
  ├─ 完整指令循环（全部类型支持）
  └─ metric 指令：ip++（跳过）

cdsl_bytecode_execute_rule:
  ├─ 简化版指令循环（仅 int 算术/比较）  ← 重复代码，功能缺失
  └─ metric 指令：自己处理（得分/违规/结束）
```

### 重构后

```
bc_run_instructions (核心循环):
  ├─ 完整指令循环（全部类型支持）
  ├─ metric 指令：若 handler != NULL 调用回调，否则 ip++
  └─ 沙箱配额检查（统一）

cdsl_bytecode_execute:
  └─ bc_run_instructions(handler=NULL, &result)

cdsl_bytecode_execute_rule:
  ├─ 分配 cdsl_rule_report_t
  ├─ bc_run_instructions(handler=metric_handler, report)
  │    ├─ 普通指令：委托核心循环（完整类型支持）
  │    ├─ BC_METRIC_START → 创建 metric 条目
  │    ├─ BC_SET_SCORE → 设置得分
  │    ├─ BC_FAIL_METRIC → 设置违规原因
  │    └─ BC_RULE_END → 跳出循环
  └─ 最终确定 report->status
```

## 6. 错误处理与边界

- **沙箱配额**：统一在 `bc_run_instructions` 中检查（指令配额、超时/OOM），两个公共函数无重复。
- **报告状态**：`error_state` 置位 → `CDSL_STATUS_ERROR`；否则按得分比例判定（≥80% PASSED，≥50% PARTIALLY_PASSED，否则 FAILED）。
- **`current_mr` 生命周期**：`BC_METRIC_START` 时分配；`SET_SCORE`/`FAIL_METRIC` 操作它，NULL 安全；报告释放由 `cdsl_report_free` 统一处理。
- **栈上溢**：保持 `CDSL_BYTECODE_MAX_STACK` (256) 固定栈，`PUSH` 边界检查不变。
- **返回值**：`bc_run_instructions` 返回 1 = 正常，0 = 中止；公共函数行为与当前一致。

## 7. 测试计划

- 现有 `tests/test_bytecode.c` 应继续通过。
- 新增测试：
  1. 字节码编译 + 执行 metric 规则：验证每个 metric 的得分、违规原因、三态状态（PASSED/PARTIALLY_PASSED/FAILED）。
  2. 字节码编译 + 执行普通 WHEN/THEN 规则：验证 float/string/date 比较结果正确（之前简化版会处理错误）。
  3. 验证 `cdsl_bytecode_execute` 行为不变（回归测试）。

## 8. 验收标准

- [ ] `src/vm/bytecode.c` 编译通过（`-Werror` 下无警告）。
- [ ] `cdsl_bytecode_execute` 和 `cdsl_bytecode_execute_rule` 公共签名不变。
- [ ] 所有 22 个现有测试通过（`ctest --test-dir build --output-on-failure`）。
- [ ] 新增测试通过，覆盖 metric 规则和普通规则的字节码执行。
- [ ] clang-tidy 无新增警告。
- [ ] 没有重复的普通指令处理代码（`cdsl_bytecode_execute_rule` 不再自行实现 PUSH/算术/比较等）。

---

**本设计文档已写入 `docs/superpowers/specs/2026-11-03-bytecode-dedup-design.md`。** 请审阅，如有修改请告诉我；如无修改，我将提交 git 并进入实施计划阶段。