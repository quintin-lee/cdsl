# C-DSL `strdup` 内存分配失败路径与异常安全设计

**Date:** 2026-11-07  
**Status:** Approved  
**Author:** C-DSL Development Team  

---

## 1. Executive Summary

C-DSL 代码库在各子系统中具有清晰且规范的 `create / free` 资源配对设计（如 `cdsl_report_free`、`cdsl_context_free`、`cdsl_schema_free` 等）。然而，代码库中约 60 处针对 `strdup()` 的调用普遍缺少对返回值为 `NULL`（即 OOM / 内存耗尽）的失败分支处理。如果系统在极端负载或低内存环境下运行，未处理的 `NULL` 将被赋给关键结构体成员（如变量名称、规则名、动作名、决策摘要等），进而引发后续在哈希计算、字符串匹配或比较（`strcmp`）过程中的空指针解引用崩溃。

本设计旨在确立统一的异常安全规范与分层回滚契约：
1. **基础工具层**：封装统一的 `cdsl_strdup(s)` 安全复制函数，提供 NULL 输入防御并强化返回值校验；
2. **独立对象层**（Context / Schema / Hashmap / VM 注册）：遵循事务性提交（Transactional commit/rollback）与强异常安全保证，分配失败时不产生任何悬挂节点，就地释放已分配的半成品并返回失败状态码；
3. **执行引擎层**（Eval / Bytecode / Report）：若在运行时构建报告或执行指令时遭遇字符串 OOM，统一将错误归口为 `CDSL_ERR_RESOURCE_LIMIT`，置位 `vm->error_state = 1`，将报告状态收敛为 `CDSL_STATUS_ERROR` 并向调用方返回详细错误信息；
4. **故障注入测试**：提供分配失败倒计时拦截钩子，在 AddressSanitizer + LeakSanitizer 环境下验证所有 OOM 路径均实现 100% 内存安全与零泄漏。

---

## 2. Current State & Problem Statement

当前代码库中 `strdup` 调用的风险点分布如下：

### 2.1 独立结构体构造与写入
* **`src/vm/context.c`**：
  * `cdsl_context_set_string`：`ne->name = strdup(name)` 与 `ne->value.data.string_val = strdup(val)`。若任意一项失败，会产生无 name 或无 string_val 的半损坏 entry 并挂入链表，后续 lookup 执行 `strcmp(e->name, ...)` 将导致 SIGSEGV。
  * `cdsl_context_set_int/float/bool/date/long`：`ne->name = strdup(name)` 失败时直接挂入链表。
  * `cdsl_context_load_json`：JSON 反序列化过程中多次 `strdup(key)` 缺少有效释放与回滚分支。
* **`src/schema/schema.c`**：
  * `cdsl_schema_register_var`：`v->name = strdup(name)` 未做校验。
  * `cdsl_schema_register_action`：`a->name = strdup(name)` 未做校验，失败会导致 `a` 残缺留在链表中。
* **`src/util/hashmap.c`**：
  * `cdsl_hashmap_put`：`e->key = strdup(key)` 失败时将空指针 key 放入桶链表中。
* **`src/vm/vm.c`**：
  * `cdsl_vm_register_action`：`e->action_name = strdup(action_name)` 未校验。
  * `cdsl_vm_register_function`：`e->func_name = strdup(func_name)` 未校验。

### 2.2 规则执行与报告构造
* **`src/vm/eval.c`**：
  * `execute_simple_rule` / `execute_metric_rule`：`report->decision_summary = strdup(buf)` 未做失败校验；指标结果循环中 `mr->metric_name = strdup(m->name)` 未做失败校验；`mr->violation_reason = strdup(...)` 未做失败校验。
* **`src/vm/bytecode.c`**：
  * `cdsl_bytecode_compile`：常量池常量字符串、`bc->rule_name`、`bc->description`、`bc->metric_infos` 复制未形成完整回滚机制。
  * `cdsl_bytecode_execute_rule`：`report->rule_name`、`report->description`、`report->decision_summary` 以及指标事件处理函数 `bc_rule_metric_handler` 中的 `mr->metric_name`、`mr->description`、`mr->violation_reason` 均未检查 `strdup` 返回值。
* **`src/vm/ruleset.c`**：
  * `batch->summary = strdup(buf)` 未做校验。
* **`src/util/error.c`**：
  * `cdsl_error_create`：`e->message = message ? strdup(message) : strdup("")`。若 OOM，`e->message` 为 NULL，打印错误或访问错误时二次解引用崩溃。

---

## 3. Design Goals

| 编号 | 目标 | 判定准则 |
|---|---|---|
| **G1** | 安全封装 | 引入 `cdsl_strdup()`，对输入 NULL 宽容返回 NULL，并统一为后续故障注入提供基础 |
| **G2** | 独立对象强异常安全 | Context、Schema、Hashmap 写入时若发生 OOM，原结构完好无损，新建对象立即回收，返回 `0` / `NULL` |
| **G3** | 执行引擎统一上报 | Eval 与 Bytecode 运行期若遭遇字符串 OOM，统一设置 `CDSL_ERR_RESOURCE_LIMIT`，返回包含完整错误原因的 `CDSL_STATUS_ERROR` 报告 |
| **G4** | 错误节点安全兜底 | 错误节点构建（`cdsl_error_create`）遭遇 OOM 时具备只读静态字符串兜底机制，杜绝二次崩溃 |
| **G5** | 故障注入可测性 | 具备确定性的内存分配失败模拟测试，验证回滚与错误分支百分百有效 |
| **G6** | ASan/LSan 零泄漏 | 在模拟故障路径下，系统无任何内存泄露与悬挂引用 |
| **G7** | API 完全向后兼容 | 现有所有公共 API 函数签名保持不变，保持 100% C23 兼容性 |

---

## 4. Design Details

### 4.1 安全字符串包装工具 (`cdsl_strdup`)

在 `include/cdsl/util/portability.h` 或 `src/vm/internal.h` 中定义安全字符串复制包装函数：

```c
/**
 * @brief Duplicate a null-terminated string with NULL and allocation safety.
 *
 * @param s Source string (may be NULL)
 * @return Newly allocated copy, or NULL if input is NULL or heap allocation fails.
 */
static inline char*
cdsl_strdup(const char* s)
{
#ifdef CDSL_TESTING_OOM_HOOK
    extern int cdsl_test_oom_check_and_decrement(void);
    if (cdsl_test_oom_check_and_decrement()) {
        return NULL; /* Simulated out of memory */
    }
#endif
    if (!s) {
        return NULL;
    }
    return strdup(s);
}
```

* **所有权法则**：返回非 NULL 指针时，调用方拥有内存所有权，最终必须通过 `free()` 释放。
* **调用前置约定**：调用方不再需要手写 `s ? strdup(s) : NULL`，直接调用 `cdsl_strdup(s)`。

---

### 4.2 独立结构体的事务性提交与回滚

#### 4.2.1 Context 模块 (`src/vm/context.c`)
针对修改现有值：
```c
int cdsl_context_set_string(cdsl_context_t* ctx, const char* name, const char* val) {
    if (!ctx || !name) return 0;
    cdsl_context_entry_t* e = cdsl_context_get_entry_internal(ctx, name);
    if (e && context_var_is_readonly(ctx, name)) return 0;

    char* new_val = val ? cdsl_strdup(val) : NULL;
    if (val && !new_val) {
        return 0; /* OOM: 保留原值，无状态损坏 */
    }

    if (e) {
        cdsl_value_free(&e->value);
        e->value.type = CDSL_TYPE_STRING;
        e->value.data.string_val = new_val;
        return 1;
    }

    /* 新建节点事务性写入 */
    char* new_name = cdsl_strdup(name);
    if (!new_name) {
        free(new_val);
        return 0;
    }

    cdsl_context_entry_t* ne = calloc(1, sizeof(*ne));
    if (!ne) {
        free(new_name);
        free(new_val);
        return 0;
    }

    ne->name = new_name;
    ne->value.type = CDSL_TYPE_STRING;
    ne->value.data.string_val = new_val;
    ne->next = ctx->entries;
    ctx->entries = ne;
    return 1;
}
```
对于数字和布尔型变体节点，若 `cdsl_strdup(name)` 失败，立即 `free(ne)` 并返回 `0`。

#### 4.2.2 Schema 模块 (`src/schema/schema.c`)
```c
int cdsl_schema_register_var(cdsl_schema_t* schema, const char* name, cdsl_type_t type) {
    ...
    cdsl_var_schema_t* v = calloc(1, sizeof(*v));
    if (!v) return 0;
    v->name = cdsl_strdup(name);
    if (!v->name) {
        free(v);
        return 0;
    }
    v->type = type;
    v->next = schema->vars;
    schema->vars = v;
    return 1;
}
```
Action 注册同理：若 `a->name` 分配失败，立即释放 `a->param_types` 与 `a` 并返回 `0`。

#### 4.2.3 Hashmap 模块 (`src/util/hashmap.c`)
```c
int cdsl_hashmap_put(cdsl_hashmap_t* map, const char* key, void* value) {
    ...
    cdsl_hashmap_entry_t* e = calloc(1, sizeof(*e));
    if (!e) return 0;
    e->key = cdsl_strdup(key);
    if (!e->key) {
        free(e);
        return 0;
    }
    e->value = value;
    e->next = map->buckets[idx];
    map->buckets[idx] = e;
    map->size++;
    return 1;
}
```

---

### 4.3 执行引擎的 OOM 状态传播与错误报告收敛

#### 4.3.1 AST 执行引擎 (`src/vm/eval.c`)
在 `execute_simple_rule` 和 `execute_metric_rule` 中：
1. **Report 基础信息构建**：
   ```c
   report->rule_name = cdsl_strdup(rule->name ? rule->name : "");
   report->description = cdsl_strdup(desc ? desc : "");
   if (!report->rule_name || !report->description) {
       goto oom_abort;
   }
   ```
2. **指标元数据与违规原因**：
   ```c
   mr->metric_name = cdsl_strdup(m->name);
   mr->description = cdsl_strdup(mdesc ? mdesc : "");
   if (!mr->metric_name || !mr->description) {
       goto oom_abort;
   }
   if (has_reason) {
       mr->violation_reason = cdsl_strdup(reason_str);
       if (!mr->violation_reason) {
           goto oom_abort;
       }
   }
   ```
3. **决策摘要生成**：
   ```c
   report->decision_summary = cdsl_strdup(buf);
   if (!report->decision_summary) {
       goto oom_abort;
   }
   ```
4. **统一 OOM 汇聚处理**：
   ```c
   oom_abort:
       cdsl_vm_set_last_error(vm, CDSL_ERR_RESOURCE_LIMIT, 0, 0,
                              "Out of memory allocating rule execution report",
                              "Increase system memory limit or reduce metric/string count");
       CDSL_ATOMIC_STORE(&vm->error_state, 1);
       cdsl_report_free(report);
       return NULL;
   ```
   外层 `cdsl_vm_execute` 检查 `CDSL_ATOMIC_LOAD(&vm->error_state)`，若 report 为 NULL 则构建状态为 `CDSL_STATUS_ERROR` 并绑定 `vm->last_error` 的标准错误报告返回。

#### 4.3.2 字节码编译与执行 (`src/vm/bytecode.c`)
* **编译期**：
  若常量池中的常量字符串（`bc_emit_string`）、`bc->rule_name`、`bc->description` 或 `bc->metric_infos` 的名称/描述在调用 `cdsl_strdup` 时失败：
  调用 `cdsl_bytecode_free(bc)` 完全清理已分配的指令数组与常量，向调用者返回 `0`。
* **执行期**：
  在 `bc_rule_metric_handler` 中：
  ```c
  mr->metric_name = cdsl_strdup(name);
  mr->description = cdsl_strdup(desc);
  if (!mr->metric_name || !mr->description) {
      return 0; /* 返回 0 告知虚拟机指令执行循环立即 abort */
  }
  ```
  主循环捕获到 handler 失败后置位 `vm->error_state = 1`，将报告状态收敛为 `CDSL_STATUS_ERROR`。

---

### 4.4 错误节点构建安全兜底 (`src/util/error.c`)

当系统由于内存枯竭引发错误时，错误结构体本身必须具备分配防崩溃能力：
```c
cdsl_error_t*
cdsl_error_create(cdsl_error_kind_t kind, int line, int column, const char* message, const char* hint)
{
    cdsl_error_t* e = calloc(1, sizeof(*e));
    if (!e) {
        return NULL;
    }
    e->kind = kind;
    e->line = line;
    e->column = column;
    
    if (message) {
        e->message = cdsl_strdup(message);
        if (!e->message) {
            /* 兜底静态安全串，防止后续访问空指针崩溃 */
            e->message = cdsl_strdup("Out of memory");
            if (!e->message) {
                free(e);
                return NULL;
            }
        }
    }
    if (hint) {
        e->hint = cdsl_strdup(hint); /* hint 为可选，允许为 NULL */
    }
    return e;
}
```

---

## 5. Testing & Fault Injection Plan

为确保每个 OOM 错误分支都得到严密测试，在测试框架中建立确定性故障注入：

### 5.1 模拟故障注入接口
在 `src/vm/internal.h` 中增加专供测试使用的计数器：
```c
#ifdef CDSL_TESTING
void cdsl_test_set_oom_countdown(int countdown);
int  cdsl_test_get_oom_countdown(void);
#endif
```
当 `countdown > 0` 时，每次 `cdsl_strdup` 调用将 `countdown` 减 1；当减到 0 时，`cdsl_strdup` 强制返回 `NULL`。若 `countdown < 0`，则正常工作不干预。

### 5.2 专项测试用例 (`tests/test_memory.c` 扩展)
1. **`test_context_oom_rollback`**：
   * 设置倒计时模拟在 `cdsl_context_set_string` 的 key 复制或 val 复制时发生 OOM。
   * 断言函数返回 `0`。
   * 断言 Context 内部无破损悬挂节点，已有键值不受任何污染。
2. **`test_schema_oom_rollback`**：
   * 模拟变量注册与动作注册时发生 OOM。
   * 断言函数返回 `0`，Schema 链表与内存保持清洁。
3. **`test_eval_report_oom_propagation`**：
   * 在执行复杂评分规则期间模拟第 N 次 `cdsl_strdup` 发生 OOM。
   * 断言 `cdsl_vm_execute` 正常返回报告且 `report->status == CDSL_STATUS_ERROR`。
   * 断言 `report->error->kind == CDSL_ERR_RESOURCE_LIMIT`。
4. **`test_bytecode_compile_oom_rollback`**：
   * 在字节码编译阶段模拟常量注入 OOM。
   * 断言 `cdsl_bytecode_compile` 返回 `0`，且 `cdsl_bytecode_free` 清理后无任何内存泄漏。
5. **ASan / LeakSanitizer 验证**：
   * 所有 OOM 测试在开启 `-fsanitize=address,undefined` 下运行，确认每一个回滚路径完全无内存泄漏。

---

## 6. Compatibility & Boundaries

* **非目标（Non-Goals）**：
  * 不修改任何公共头文件中的函数入参或返回值类型。
  * 不使用非标准的 setjmp/longjmp 或异常抛出机制。
* **边界约束**：
  * 保持严格 C23 与 `-Wall -Wextra -Wpedantic -Werror` 零编译警告要求。
  * 保证全部 28 个 C 测试与 104 个 Python 测试套件在正常工作模式下 100% 保持通过。
