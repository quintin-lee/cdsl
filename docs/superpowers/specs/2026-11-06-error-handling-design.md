# C-DSL 错误处理增强设计 (D1, D2)

**Date:** 2026-11-06  
**Subproject:** 2 — 错误处理增强 (D1, D2)  
**Status:** Draft  
**Author:** C-DSL Development Team

## 1. Executive Summary

在保留现有所有公共 API（向后兼容）的前提下，增强错误处理能力：D1 增加运行时错误的详情捕获与上报（eval.c/bytecode.c），D2 增强编译期/验证期错误的源码位置信息（schema.c 验证错误、parser 错误）。新错误详情通过已有的 `cdsl_error_t` 结构体（line/column/message/hint/kind）暴露，不引入新的公共 API 类型。

## 2. Current State

**D1 — 运行时错误**：
- `cdsl_vm_t` 中仅有 `cdsl_atomic_int_t error_state` 布尔标志，表示是否发生运行时错误
- `cdsl_rule_report_t` 仅有 `cdsl_rule_status_t status`，可设为 `CDSL_STATUS_ERROR`，但无具体错误消息、无行列号、无错误类型
- `eval.c`/`bytecode.c` 中多处 `CDSL_ATOMIC_STORE(&vm->error_state, 1)`，但不记录错误原因
- 用户无法得知运行时出错的具体原因（除零、类型不匹配、未定义变量等）

**D2 — 编译期/验证期错误**：
- `cdsl_verify_rule_detailed()` 已返回 `cdsl_error_list_t*`，每个错误带 `line`/`column`/`message`/`hint`/`kind`
- 但验证阶段生成的错误 `line`/`column` 均为 0，无法定位到源码位置
- AST 节点（`cdsl_expr_node_t` 等）没有 `line`/`column` 字段，无法关联源码位置
- Parser 已有 column tracking（Subproject E 已完成），语法错误可携带列号，但验证阶段无法复用

## 3. Design Goals

| Goal | Description |
|------|-------------|
| G1 | D1: `cdsl_rule_report_t` 新增 `cdsl_error_t* error` 字段，携带运行时错误详情 |
| G2 | D1: `cdsl_vm_t` 新增 `cdsl_error_t* last_error` 字段，记录上次执行的最后一次运行时错误 |
| G3 | D1: 新增 `cdsl_vm_get_last_error(cdsl_vm_t*)` 访问器 |
| G4 | D1: 运行时异常（除零、类型不匹配、未定义变量等）时填充 `last_error` 的具体消息 |
| G5 | D2: AST 节点新增 `int line`、`int column` 字段，解析时填充 |
| G6 | D2: `cdsl_verify_rule_detailed` 从 AST 节点读取 `line`/`column` 填充错误 |
| G7 | 现有所有公共 API 完全保留，向后兼容，现有代码零改动 |
| G8 | 仅新增，不删除、不修改任何现有函数签名 |

## 4. Design Details

### 4.1 D1: 运行时错误详情

**`cdsl_rule_report_t`** (在 `include/cdsl/report.h` 中) 新增：

```c
typedef struct cdsl_rule_report {
    char* rule_name;
    char* description;
    cdsl_metric_result_t* metrics;
    int metric_count;
    int total_max_score;
    int total_obtained_score;
    cdsl_rule_status_t status;
    char* decision_summary;
    cdsl_error_t* error;   /**< NEW: 运行时错误详情，NULL 表示无错误；report 拥有所有权，cdsl_report_free 释放 */
} cdsl_rule_report_t;
```

**`cdsl_vm_t`** (在 `include/cdsl/vm.h` 中) 新增：

```c
typedef struct cdsl_vm {
    ...
    cdsl_atomic_int_t error_state;      /**< 运行时错误标志（现有） */
    cdsl_error_t* last_error;           /**< NEW: 上次执行的最后一次运行时错误详情 */
    ...
} cdsl_vm_t;
```

**新增访问器** (在 `include/cdsl/vm.h` 中)：

```c
/**
 * @brief Get the last runtime error from the VM.
 *
 * Returns the error details from the most recent cdsl_vm_execute() call
 * that encountered a runtime error.  The returned pointer is borrowed;
 * do not free it.
 *
 * @param vm  VM instance
 * @return    Last runtime error, or NULL if no error occurred
 */
CDSL_NODISCARD
const cdsl_error_t* cdsl_vm_get_last_error(const cdsl_vm_t* vm);
```

**错误填充位点** (`eval.c`、`bytecode.c`)：所有当前执行 `CDSL_ATOMIC_STORE(&vm->error_state, 1)` 的路径，改为同时调用新增的内部辅助函数 `cdsl_vm_set_last_error(vm, kind, line, column, message, hint)`，记录具体错误消息。

在 `cdsl_vm_execute` 结束时，若 `error_state` 为真且 report 非 NULL，将 `vm->last_error` 深拷贝附加到 `rpt->error`（report 负责 free）。

### 4.2 D2: 验证期错误源码位置

**AST 节点** (在 `include/cdsl/ast.h` 中) 新增：

```c
typedef struct cdsl_expr_node {
    ...
    int line;   /**< NEW: Source line number (0 if unknown) */
    int column; /**< NEW: Source column number (0 if unknown) */
    ...
} cdsl_expr_node_t;
```

同理为 `cdsl_action_node_t`、`cdsl_rule_t` 等主要 AST 节点添加 `line`/`column`。

**Parser** (`parser/lexer.l`、`parser/parser.y`)：解析时将当前行号/列号传给 AST 节点构造函数（Subproject E 已完成 column tracking，可复用）。

**`cdsl_verify_rule_detailed`** (`src/schema/schema.c`)：在生成 `cdsl_error_t` 时，从相关的 AST 节点读取 `line`/`column` 填充错误，而非全传 0。

**错误打印增强** (`src/util/error.c`)：`cdsl_error_print` / `cdsl_error_list_print` 输出格式增强，使 line/column 信息更醒目（可选，保持向后兼容）。

### 4.3 Files Affected

| File | Change |
|------|--------|
| `include/cdsl/report.h` | 修改 — `cdsl_rule_report_t` 增加 `cdsl_error_t* error` |
| `include/cdsl/vm.h` | 修改 — `cdsl_vm_t` 增加 `last_error`；新增 `cdsl_vm_get_last_error()` |
| `include/cdsl/ast.h` | 修改 — AST 节点增加 `line`/`column` 字段 |
| `include/cdsl/util/error.h` | 修改 (可选) — 错误打印 API 增强 |
| `src/vm/vm.c` | 修改 — `cdsl_vm_create/free` 管理 `last_error`；`cdsl_vm_execute` 附加错误到 report |
| `src/vm/eval.c` | 修改 — 运行时错误路径填充 `last_error` |
| `src/vm/bytecode.c` | 修改 — 运行时错误路径填充 `last_error` |
| `src/ast/ast.c` | 修改 — AST 节点创建时设置 `line`/`column` |
| `parser/lexer.l` | 修改 — 解析时传入位置信息 |
| `parser/parser.y` | 修改 — 解析时传入位置信息 |
| `src/schema/schema.c` | 修改 — `cdsl_verify_rule_detailed` 从 AST 节点读取 line/column |
| `src/util/error.c` | 修改 (可选) — 错误打印输出增强 |
| `tests/test_runtime_errors.c` | **New** — D1 运行时错误测试 |
| `tests/test_validation_errors.c` | **New** — D2 验证期错误位置测试 |

## 5. Backward Compatibility

| Existing API | Status | Notes |
|--------------|--------|-------|
| `cdsl_vm_create/free/register_action/...` | 保留 | 无需修改 |
| `cdsl_vm_execute` 返回 `cdsl_rule_report_t*` | 保留 | 结构体新增指针字段，现有访问代码无影响 |
| `cdsl_rule_report_t.status` | 保留 | 语义不变 |
| `cdsl_error_t` 结构体 | 保留 | `line`/`column` 现在会被填充非零值 |
| `cdsl_verify_rule_detailed` | 保留 | 返回的错误现在带有正确 line/column |
| 新增 `cdsl_vm_get_last_error()` | 新增 | 可选使用 |
| 新增 `cdsl_rule_report_t.error` | 新增 | 可选使用 |
| AST 节点新增 `line`/`column` | 新增 | 现有构造代码设置默认值 0 |

## 6. Test Plan

| Test File | Description |
|-----------|-------------|
| `tests/test_runtime_errors.c` | D1: 除零、类型不匹配、未定义变量等运行时错误；验证 `report->error` 非 NULL 且消息正确 |
| `tests/test_validation_errors.c` | D2: schema 验证错误携带正确 line/column；parser 语法错误携带列号 |
| Existing `tests/test_error.c` | 继续通过（向后兼容） |
| Existing 24 tests | 继续通过 |

## 7. Success Criteria

- [ ] D1: `cdsl_rule_report_t.error` 在运行时出错时非 NULL，携带具体消息
- [ ] D1: `cdsl_vm_get_last_error()` 返回正确的错误详情
- [ ] D2: `cdsl_verify_rule_detailed` 返回的错误 `line`/`column` 非零，能定位源码
- [ ] 所有 24 个现有测试继续通过（向后兼容）
- [ ] 新增错误测试全部通过
- [ ] ASan 构建无泄漏、无崩溃
- [ ] 现有 API 函数签名无变动

## 8. Files Created

| File | Purpose |
|------|---------|
| `tests/test_runtime_errors.c` | D1 运行时错误测试 |
| `tests/test_validation_errors.c` | D2 验证期错误位置测试 |

## 9. Files Modified

| File | Change |
|------|--------|
| `include/cdsl/report.h` | `cdsl_rule_report_t` 增加 `error` 字段 |
| `include/cdsl/vm.h` | `cdsl_vm_t` 增加 `last_error`；新增 `cdsl_vm_get_last_error()` |
| `include/cdsl/ast.h` | AST 节点增加 `line`/`column` |
| `src/vm/vm.c` | 管理 `last_error` 生命周期，附加到 report |
| `src/vm/eval.c` | 运行时错误填充 `last_error` |
| `src/vm/bytecode.c` | 运行时错误填充 `last_error` |
| `src/ast/ast.c` | AST 节点创建设置 `line`/`column` |
| `parser/lexer.l` | 解析时传入位置信息 |
| `parser/parser.y` | 解析时传入位置信息 |
| `src/schema/schema.c` | 验证错误从 AST 节点读取 line/column |
| `src/util/error.c` | 错误打印输出增强（可选） |

---

**本设计文档将写入 `docs/superpowers/specs/2026-11-06-error-handling-design.md`。** 请审阅，如有修改请告诉我；如无修改，我将提交 git 并进入实施计划阶段。