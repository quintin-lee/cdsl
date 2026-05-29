# 模块设计文档

## 模块总览

```
cdsl/
├── 核心模块
│   ├── ast          - 抽象语法树定义与构建
│   ├── abstract     - Schema 校验与类型检查
│   ├── execution    - VM 执行引擎与报告生成
│   └── ai_bridge    - AI 翻译与安全审查
│
├── 基础设施
│   ├── cdsl_json    - 零依赖 JSON 解析器
│   ├── cdsl_error   - 结构化错误报告
│   ├── cdsl_arena   - Arena 内存分配器
│   └── cdsl_hashmap - 哈希表实现
│
├── 语法定义
│   ├── lexer.l      - Flex 词法规则
│   └── parser.y     - Bison 语法规则
│
└── 构建与文档
    ├── CMakeLists.txt
    ├── Doxyfile
    └── cmake/       - 包配置模板
```

---

## 1. AST 模块 (`ast.h` / `ast.c`)

### 职责
定义 DSL 的所有语法节点类型，提供节点构建和内存释放 API。

### 核心数据结构

```
cdsl_rule_t (规则)
├── name                   - 规则名称
├── meta_list              - 元数据链表 (description, weight, ...)
├── when_expr              - WHEN 表达式 (简单规则)
├── then_action            - THEN 动作 (简单规则)
└── metrics                - METRIC 链表 (评分规则)
    └── cdsl_metric_node_t
        ├── name           - 指标名称
        ├── meta_list      - 指标元数据 (weight, is_critical)
        ├── case_list      - CASE 分支链表
        │   └── cdsl_case_node_t
        │       ├── condition  - 条件表达式
        │       └── action     - 命中动作
        └── default_action - DEFAULT 动作
```

### 表达式节点类型

| 类型 | 数据 | 示例 |
|---|---|---|
| `CDSL_EXPR_ID` | 变量名 | `user.age` |
| `CDSL_EXPR_INT` | 整数值 | `18` |
| `CDSL_EXPR_FLOAT` | 浮点值 | `3.14` |
| `CDSL_EXPR_BOOL` | 布尔值 | `true` |
| `CDSL_EXPR_STRING` | 字符串 | `"hello"` |
| `CDSL_EXPR_BINARY` | 二元运算 | `a + b`, `x == y` |
| `CDSL_EXPR_UNARY` | 一元运算 | `!flag` |

### 关键 API

```c
// 解析 DSL 字符串为 AST
cdsl_rule_t* cdsl_parse_string(const char* dsl_code);

// 构建表达式节点
cdsl_expr_node_t* cdsl_create_expr_binary(cdsl_op_t op, cdsl_expr_node_t* left, cdsl_expr_node_t* right);

// 释放整个规则树
void cdsl_free_rule(cdsl_rule_t* rule);
```

---

## 2. Abstract 模块 (`abstract.h` / `abstract.c`)

### 职责
Schema 注册和规则静态验证。在执行前拦截类型错误和未定义变量。

### Schema 结构

```
cdsl_schema_t
├── vars (cdsl_var_schema_t 链表)
│   └── { name: "user.age", type: CDSL_TYPE_INT }
│   └── { name: "user.name", type: CDSL_TYPE_STRING }
│   └── ...
└── actions (cdsl_action_schema_t 链表)
    └── { name: "block", return: VOID, arg_count: 1, arg_types: [STRING] }
    └── { name: "score", return: VOID, arg_count: 1, arg_types: [INT] }
    └── ...
```

### 验证流程

```
cdsl_verify_rule(rule, schema)
    │
    ├─ 遍历所有表达式节点
    │   └─ resolve_expr_type() → 查找变量类型，检查类型兼容性
    │
    ├─ 遍历所有 Action 调用
    │   └─ verify_action() → 查找 Action Schema，检查参数个数和类型
    │
    └─ 返回 1 (通过) 或 0 (失败，err_buf 包含错误信息)
```

### 错误报告

`cdsl_verify_rule_detailed()` 返回 `cdsl_error_list_t`，包含所有发现的错误：

```c
cdsl_error_list_t* errors = cdsl_verify_rule_detailed(rule, schema);
for (int i = 0; i < errors->count; i++) {
    cdsl_error_print(errors->errors[i]);
    // [TYPE] line 0, col 0: Unknown variable 'unknown.var'
    //   hint: Register variable 'unknown.var' via cdsl_schema_register_var()
}
```

---

## 3. Execution 模块 (`execution.h` / `execution.c`)

### 职责
AST 解释执行、上下文绑定、Action 回调分发、报告生成。

### 上下文 (Context)

```
cdsl_context_t
├── schema   - 关联的 Schema
└── entries  - 变量绑定链表
    ├── { name: "user.age", value: {INT, 25} }
    ├── { name: "user.name", value: {STRING, "Alice"} }
    └── ...
```

支持两种绑定方式：
1. **API 绑定**: `cdsl_context_set_int(ctx, "user.age", 25)`
2. **JSON 加载**: `cdsl_context_load_json(ctx, "{\"user\":{\"age\":25}}")`

### VM 执行流程

```
cdsl_vm_execute(vm, rule, ctx)
    │
    ├─ 简单规则 (rule->metrics == NULL)
    │   ├─ eval_expr(rule->when_expr, ctx) → bool
    │   ├─ if true: trigger_action(rule->then_action) → FAILED
    │   └─ if false: → PASSED
    │
    └─ 评分规则 (rule->metrics != NULL)
        ├─ for each metric:
        │   ├─ for each case:
        │   │   ├─ eval_expr(case.condition, ctx)
        │   │   └─ if true: trigger_action(case.action), break
        │   ├─ if no case matched: trigger_action(default_action)
        │   ├─ check is_critical → veto if failed
        │   └─ accumulate score
        ├─ aggregate total score
        ├─ check thresholds (pass_threshold, partial_threshold)
        └─ determine tri-state status
```

### 三态判定逻辑

```
if (any_critical_metric_failed):
    status = FAILED (一票否决)
else if (total_score >= pass_threshold):
    status = PASSED
else if (total_score >= partial_threshold):
    status = PARTIALLY_PASSED
else:
    status = FAILED
```

### RuleSet 批量执行

```
cdsl_ruleset_t
├── entries (按 priority 排序的链表)
│   ├── { rule: r1, priority: 1 }  ← 先执行
│   ├── { rule: r2, priority: 2 }
│   └── { rule: r3, priority: 3 }  ← 后执行
└── count: 3

cdsl_vm_execute_ruleset(vm, set, ctx)
    → cdsl_ruleset_report_t
        ├── rule_reports[]  - 每条规则的详细报告
        ├── total_passed    - 通过数
        ├── total_failed    - 失败数
        ├── aggregate_score - 汇总分数
        └── summary         - 摘要字符串
```

---

## 4. AI Bridge 模块 (`ai_bridge.h` / `ai_bridge.c`)

### 职责
自然语言转 DSL 翻译，DSL 规则安全性审查。

### 工作模式

| 模式 | `use_mock` | 说明 |
|---|---|---|
| Mock 模式 | 1 | 离线关键词匹配翻译，结构化审查 |
| API 模式 | 0 | 调用 OpenAI-compatible LLM API |

### Mock 翻译逻辑

根据自然语言中的关键词匹配预定义模板：

| 关键词 | 生成规则 |
|---|---|
| `supplier` / `供应商` / `资质` | 供应商资质审查规则 |
| `document` / `文档` / `格式` | 文档格式审查规则 |
| `content` / `内容` / `安全` | 内容安全审查规则 |

### 安全审查评分

Mock 模式下的审查评分（满分 70 分）：

| 检查项 | 分值 | 说明 |
|---|---|---|
| META 块存在 | +10 | 规则有元数据描述 |
| METRIC 块存在 | +15 | 使用多指标结构 |
| CASE + DEFAULT 完整 | +15 | 每个指标有完整分支 |
| is_critical 标记 | +10 | 有红线合规项 |
| weight 权重 | +10 | 指标有权重分配 |
| description 描述 | +10 | 有功能描述 |

分数 ≥ 50 → approved = 1

---

## 5. 基础设施模块

### 5.1 JSON 解析器 (`cdsl_json.h` / `cdsl_json.c`)

零依赖的轻量 JSON 解析器，支持：
- 对象、数组、字符串、数字、布尔、null
- 嵌套结构
- 被 `cdsl_context_load_json()` 使用

### 5.2 错误报告 (`cdsl_error.h` / `cdsl_error.c`)

结构化错误类型：
- `CDSL_ERR_SYNTAX` - 语法错误
- `CDSL_ERR_TYPE` - 类型错误
- `CDSL_ERR_SEMANTIC` - 语义错误
- `CDSL_ERR_RUNTIME` - 运行时错误

### 5.3 Arena 分配器 (`cdsl_arena.h` / `cdsl_arena.c`)

批量内存分配器，适合 AST 节点等共享生命周期的对象：
- 8 字节对齐
- 默认 64KB 块大小
- 一次释放所有内存

### 5.4 哈希表 (`cdsl_hashmap.h` / `cdsl_hashmap.c`)

O(1) 平均查找的键值表：
- Separate chaining 解决冲突
- 字符串键，泛型值指针
- 可选析构回调
