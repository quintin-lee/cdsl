# 用户手册

## 1. 快速开始

### 1.1 环境要求

- C 编译器 (GCC / Clang)
- CMake 3.14+
- Flex 2.6+
- Bison 3.8+

### 1.2 构建项目

```bash
# 克隆项目
git clone <repo-url>
cd dsl

# 构建
mkdir build && cd build
cmake ..
make -j$(nproc)

# 运行演示
./cdsl_demo

# 运行测试
ctest

# 生成文档
make doc
# 文档位于 docs/html/index.html
```

### 1.3 集成到宿主项目

**方式一：add_subdirectory**

```cmake
# 在你的 CMakeLists.txt 中
add_subdirectory(path/to/cdsl)
target_link_libraries(your_app PRIVATE cdsl)
```

**方式二：安装后使用**

```bash
cd build && cmake --install . --prefix /usr/local
```

```cmake
find_package(cdsl REQUIRED)
target_link_libraries(your_app PRIVATE cdsl::cdsl_static)
```

**方式三：pkg-config**

```bash
pkg-config --cflags --libs cdsl
```

---

## 2. 核心 API 使用

### 2.1 定义 Schema

Schema 是规则与宿主程序之间的契约，注册所有可用的变量和动作。

```c
#include "abstract.h"

cdsl_schema_t* schema = cdsl_schema_create();

// 注册变量
cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
cdsl_schema_register_var(schema, "user.name", CDSL_TYPE_STRING);
cdsl_schema_register_var(schema, "user.is_active", CDSL_TYPE_BOOL);

// 注册动作
cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
cdsl_schema_register_action(schema, "score", CDSL_TYPE_VOID, 1, CDSL_TYPE_INT);
cdsl_schema_register_action(schema, "fail_metric", CDSL_TYPE_VOID, 2, CDSL_TYPE_INT, CDSL_TYPE_STRING);
```

### 2.2 解析规则

```c
#include "ast.h"

const char* dsl = 
    "RULE check_age {"
    "  META { description = \"Age check\" }"
    "  WHEN user.age >= 18"
    "  THEN block(\"adult\")"
    "}";

cdsl_rule_t* rule = cdsl_parse_string(dsl);
if (!rule) {
    fprintf(stderr, "Parse error\n");
    return;
}
```

### 2.3 验证规则

```c
#include "abstract.h"

// 方式一：简单验证
char err[512] = {0};
if (!cdsl_verify_rule(rule, schema, err, sizeof(err))) {
    fprintf(stderr, "Verification failed: %s\n", err);
}

// 方式二：详细验证（收集所有错误）
cdsl_error_list_t* errors = cdsl_verify_rule_detailed(rule, schema);
if (errors->count > 0) {
    cdsl_error_list_print(errors);
}
cdsl_error_list_free(errors);
```

### 2.4 创建 VM 并注册动作

```c
#include "execution.h"

// 动作回调
void on_block(const char* name, cdsl_arg_node_t* args, void* ud) {
    if (args && args->expr && args->expr->type == CDSL_EXPR_STRING) {
        printf("BLOCKED: %s\n", args->expr->data.string_val);
    }
}

cdsl_vm_t* vm = cdsl_vm_create(schema);
cdsl_vm_register_action(vm, "block", on_block);
cdsl_vm_register_action(vm, "score", on_score);
cdsl_vm_register_action(vm, "fail_metric", on_fail_metric);
```

### 2.5 绑定上下文并执行

```c
// 方式一：API 绑定
cdsl_context_t* ctx = cdsl_context_create(schema);
cdsl_context_set_int(ctx, "user.age", 25);
cdsl_context_set_string(ctx, "user.name", "Alice");
cdsl_context_set_bool(ctx, "user.is_active", 1);

// 方式二：JSON 加载
cdsl_context_load_json(ctx, "{\"user\":{\"age\":25,\"name\":\"Alice\",\"is_active\":true}}");

// 执行
cdsl_rule_report_t* report = cdsl_vm_execute(vm, rule, ctx);

// 查看结果
cdsl_report_print(report);
// 或序列化为 JSON
char* json = cdsl_report_to_json(report);
printf("%s\n", json);
free(json);

// 清理
cdsl_report_free(report);
cdsl_context_free(ctx);
cdsl_vm_free(vm);
cdsl_free_rule(rule);
cdsl_schema_free(schema);
```

---

## 3. 评分规则使用

### 3.1 定义评分规则

```c
const char* dsl =
    "RULE scoring {"
    "  META { description = \"Test\" pass_threshold = \"80\" partial_threshold = \"50\" }"
    "  METRIC m1 {"
    "    META { description = \"Metric 1\" weight = \"60\" }"
    "    CASE user.age >= 18 THEN score(60)"
    "    DEFAULT score(0)"
    "  }"
    "  METRIC m2 {"
    "    META { description = \"Metric 2\" weight = \"40\" is_critical = \"true\" }"
    "    CASE user.is_active == true THEN score(40)"
    "    DEFAULT fail_metric(0, \"inactive\")"
    "  }"
    "}";
```

### 3.2 三态结果

```c
cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);

switch (rpt->status) {
    case CDSL_STATUS_PASSED:
        printf("通过: %d/%d\n", rpt->total_obtained_score, rpt->total_max_score);
        break;
    case CDSL_STATUS_PARTIALLY_PASSED:
        printf("部分通过: %d/%d\n", rpt->total_obtained_score, rpt->total_max_score);
        break;
    case CDSL_STATUS_FAILED:
        printf("未通过: %d/%d\n", rpt->total_obtained_score, rpt->total_max_score);
        for (int i = 0; i < rpt->metric_count; i++) {
            if (!rpt->metrics[i].is_passed && rpt->metrics[i].violation_reason) {
                printf("  失败项: %s - %s\n", rpt->metrics[i].metric_name, rpt->metrics[i].violation_reason);
            }
        }
        break;
}
```

---

## 4. 批量执行

### 4.1 RuleSet 使用

```c
cdsl_ruleset_t* set = cdsl_ruleset_create();

// 添加规则（数字越小优先级越高）
cdsl_ruleset_add(set, rule_high_priority, 1);
cdsl_ruleset_add(set, rule_medium_priority, 5);
cdsl_ruleset_add(set, rule_low_priority, 10);

// 批量执行
cdsl_ruleset_report_t* batch = cdsl_vm_execute_ruleset(vm, set, ctx);
cdsl_ruleset_report_print(batch);

// 查看汇总
printf("通过: %d, 部分通过: %d, 失败: %d\n",
       batch->total_passed, batch->total_partially, batch->total_failed);
printf("总分: %d/%d\n", batch->aggregate_score, batch->aggregate_max);

cdsl_ruleset_report_free(batch);
cdsl_ruleset_free(set);
```

---

## 5. AI 集成

### 5.1 Mock 模式（离线演示）

```c
#include "ai_bridge.h"

cdsl_ai_config_t cfg = cdsl_ai_config_default(); // use_mock = 1

// 自然语言转 DSL
char* dsl = cdsl_ai_translate("供应商资质审查，黑名单一票否决", schema, &cfg);
printf("Generated DSL:\n%s\n", dsl);

// 规则安全审查
cdsl_ai_review_t* review = cdsl_ai_review(dsl, schema, &cfg);
printf("Approved: %s, Risk: %d\n", review->approved ? "YES" : "NO", review->risk_score);
printf("Reason: %s\n", review->reason);

cdsl_ai_review_free(review);
free(dsl);
```

### 5.2 API 模式（真实 LLM）

```c
cdsl_ai_config_t cfg = {
    .use_mock = 0,
    .api_key = getenv("OPENAI_API_KEY"),
    .api_base = "https://api.openai.com/v1",
    .model = "gpt-4o-mini"
};

char* dsl = cdsl_ai_translate("check if transaction amount exceeds limit", schema, &cfg);
```

---

## 6. 自定义动作

宿主程序可以注册任意 C 函数作为动作回调：

```c
// 自定义动作：发送告警
void alert_handler(const char* name, cdsl_arg_node_t* args, void* ud) {
    const char* reason = "unknown";
    if (args && args->expr && args->expr->type == CDSL_EXPR_STRING) {
        reason = args->expr->data.string_val;
    }
    // 调用你的告警系统
    send_alert(reason);
}

cdsl_vm_register_action(vm, "send_alert", alert_handler);
```

然后在 DSL 中使用：
```dsl
RULE alert_rule {
    META { description = "Alert on high value" }
    WHEN transaction.amount > 100000
    THEN send_alert("high_value_transaction")
}
```

---

## 7. 错误处理

### 7.1 解析错误

```c
cdsl_rule_t* rule = cdsl_parse_string("INVALID DSL CODE");
if (!rule) {
    // Bison 会自动打印错误到 stderr:
    // Syntax Error: syntax error at line 1
}
```

### 7.2 验证错误

```c
cdsl_error_list_t* errors = cdsl_verify_rule_detailed(rule, schema);
for (int i = 0; i < errors->count; i++) {
    cdsl_error_t* e = errors->errors[i];
    fprintf(stderr, "[%s] line %d: %s\n",
            e->kind == CDSL_ERR_TYPE ? "TYPE" : "SEMANTIC",
            e->line, e->message);
    if (e->hint) fprintf(stderr, "  hint: %s\n", e->hint);
}
cdsl_error_list_free(errors);
```

---

## 8. 线程安全注意事项

| 操作 | 线程安全 |
|---|---|
| `cdsl_parse_string()` | ❌ 不安全（Flex 全局状态） |
| `cdsl_vm_execute()` | ✅ 安全（每线程独立 VM） |
| `cdsl_context_*()` | ✅ 安全（每线程独立 Context） |
| Schema 只读访问 | ✅ 安全 |
| Rule 只读访问 | ✅ 安全 |

**建议**: 每个线程创建独立的 VM 和 Context。Schema 和 Rule 可以跨线程共享（只读）。
