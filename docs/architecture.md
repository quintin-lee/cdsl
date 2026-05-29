# C-DSL 架构文档

## 1. 概述

C-DSL 是一套基于 C 语言的领域特定语言（DSL）规则引擎框架，专为业务规则校验场景设计。支持自然语言描述的规则自动转为 DSL 语句，并通过 AI 审查 + 规则校验双重机制确保规则的正确性和安全性。

### 1.1 设计目标

| 目标 | 说明 |
|---|---|
| **零外部依赖** | 核心库无任何第三方依赖，JSON 解析器自行实现 |
| **三层架构** | 语法层 → 抽象层 → 执行层，职责清晰 |
| **AI 驱动** | 支持自然语言转 DSL，AI 规则安全性审查 |
| **多指标评分** | 支持加权评分、红线一票否决、三态输出 |
| **易于集成** | CMake 一键集成，支持静态库/共享库/pkg-config |

### 1.2 技术栈

- **语言**: C99
- **构建**: CMake 3.14+
- **语法解析**: Flex 2.6+ / Bison 3.8+
- **文档**: Doxygen
- **测试**: 自定义轻量测试框架

---

## 2. 三层架构

```
┌──────────────────────────────────────────────────────────┐
│                   AI Bridge Layer                         │
│         (Natural Language → DSL → Safety Review)         │
└────────────────────────────┬─────────────────────────────┘
                             │ Generated DSL String
                             ▼
┌──────────────────────────────────────────────────────────┐
│  1. Syntax Layer (语法层)                                  │
│     Flex (Lexer) ──► Token Stream ──► Bison (Parser)     │
│                                          │                │
│                                          ▼                │
│                                    AST (语法树)            │
└──────────────────────────────┬───────────────────────────┘
                               │ Raw AST
                               ▼
┌──────────────────────────────────────────────────────────┐
│  2. Abstract Layer (抽象层)                                │
│     Schema Registration ──► Type Checking ──► Verify      │
│     (保证变量存在、类型安全、防止运行时越界)                   │
└──────────────────────────────┬───────────────────────────┘
                               │ Verified AST
                               ▼
┌──────────────────────────────────────────────────────────┐
│  3. Execution Layer (执行层)                               │
│     Context Binding ──► AST Interpreter ──► Report        │
│     (绑定上下文变量，解释执行 AST，生成评分报告)              │
└──────────────────────────────────────────────────────────┘
```

### 2.1 语法层 (Syntax Layer)

**职责**: 将 DSL 源码文本解析为抽象语法树（AST）。

**组件**:
- `parser/lexer.l` - Flex 词法分析器，将源码拆分为 Token 流
- `parser/parser.y` - Bison 语法分析器，将 Token 流构建为 AST
- `src/ast.c` - AST 节点构建与内存管理

**关键数据流**:
```
DSL Source String → Lexer (Tokenize) → Parser (Build AST) → cdsl_rule_t
```

### 2.2 抽象层 (Abstract Layer)

**职责**: 对 AST 进行静态分析，确保类型安全和语义正确。

**组件**:
- `src/abstract.c` - 类型检查器和语义验证器
- `include/cdsl_error.h` - 结构化错误报告

**检查内容**:
- 变量是否存在（通过 Schema 查找）
- 操作数类型是否匹配（如 INT 与 STRING 不能比较）
- Action 参数个数和类型是否正确
- 收集所有错误而非首个即停

### 2.3 执行层 (Execution Layer)

**职责**: 解释执行 AST，绑定运行时上下文，生成评估报告。

**组件**:
- `src/execution.c` - AST 解释器、上下文管理、报告生成
- `src/cdsl_json.c` - JSON 上下文加载器

**执行流程**:
```
1. 绑定上下文变量 (cdsl_context_t)
2. 遍历 AST 节点求值 (eval_expr)
3. 对 METRIC 规则：逐个 CASE 匹配，累计分数
4. 对简单规则：计算 WHEN 表达式，触发 THEN 动作
5. 根据分数和阈值判定三态结果
6. 生成结构化报告 (cdsl_rule_report_t)
```

---

## 3. 数据流

### 3.1 简单规则执行流

```
输入: "RULE check { WHEN user.age > 18 THEN block(\"adult\") }"
                          │
                          ▼
                    ┌─────────────┐
                    │   Parser    │
                    └──────┬──────┘
                           │ cdsl_rule_t
                           ▼
                    ┌─────────────┐
                    │  Verifier   │ ← Schema (user.age: INT, block(STRING))
                    └──────┬──────┘
                           │ Verified AST
                           ▼
                    ┌─────────────┐
                    │     VM      │ ← Context {user.age: 25}
                    └──────┬──────┘
                           │
                           ▼
                    ┌─────────────┐
                    │   Report    │ → PASSED / FAILED
                    └─────────────┘
```

### 3.2 多指标评分执行流

```
输入: RULE scoring { METRIC m1 { CASE ... THEN score(40) } METRIC m2 { ... } }
                          │
                          ▼
                    ┌─────────────┐
                    │   Parser    │
                    └──────┬──────┘
                           │ cdsl_rule_t (metrics != NULL)
                           ▼
                    ┌─────────────┐
                    │  Verifier   │
                    └──────┬──────┘
                           ▼
                    ┌─────────────┐
                    │     VM      │
                    └──────┬──────┘
                           │
              ┌────────────┼────────────┐
              ▼            ▼            ▼
         ┌────────┐  ┌────────┐  ┌────────┐
         │ METRIC │  │ METRIC │  │ METRIC │
         │  m1    │  │  m2    │  │  m3    │
         │ 40/40  │  │ 20/30  │  │ 0/30 * │ ← is_critical
         └────────┘  └────────┘  └────────┘
              │            │            │
              └────────────┼────────────┘
                           ▼
                    ┌─────────────┐
                    │ Aggregator  │ → Score: 60/100
                    │ + Threshold │ → Status: PARTIALLY PASSED
                    │ + Critical  │
                    └──────┬──────┘
                           ▼
                    ┌─────────────┐
                    │   Report    │
                    └─────────────┘
```

---

## 4. 线程安全

- 每个线程应创建独立的 `cdsl_vm_t` 和 `cdsl_context_t`
- `cdsl_schema_t` 可以跨线程共享（只读）
- `cdsl_rule_t` 解析后可以跨线程共享（只读）
- Flex/Bison 解析器使用全局状态，**不支持并发解析**，需加锁或使用独立进程

---

## 5. 内存管理

| 对象 | 分配方式 | 释放方式 |
|---|---|---|
| `cdsl_rule_t` | `malloc` | `cdsl_free_rule()` |
| `cdsl_context_t` | `malloc` | `cdsl_context_free()` |
| `cdsl_vm_t` | `malloc` | `cdsl_vm_free()` |
| `cdsl_report_t` | `malloc` | `cdsl_report_free()` |
| `cdsl_ruleset_t` | `malloc` | `cdsl_ruleset_free()` |
| `cdsl_schema_t` | `malloc` | `cdsl_schema_free()` |
| `cdsl_arena_t` | `malloc` | `cdsl_arena_free()` (批量释放) |

**注意**: `cdsl_parse_string()` 返回的 rule 的内部字符串由 parser 复制，用户需调用 `cdsl_free_rule()` 释放。
