# DSL 语法说明

## 1. 词法规则

### 1.1 关键字

| 关键字 | 说明 |
|---|---|
| `RULE` | 规则定义开始 |
| `META` | 元数据块开始 |
| `WHEN` | 条件表达式（简单规则） |
| `THEN` | 动作声明（简单规则） |
| `METRIC` | 指标块开始（评分规则） |
| `CASE` | 条件分支（评分规则） |
| `DEFAULT` | 默认分支（评分规则） |
| `AND` / `&&` | 逻辑与 |
| `OR` / `\|\|` | 逻辑或 |
| `NOT` / `!` | 逻辑非 |
| `true` / `false` | 布尔字面量 |

### 1.2 运算符

| 运算符 | 说明 | 返回类型 |
|---|---|---|
| `==` | 等于 | BOOL |
| `!=` | 不等于 | BOOL |
| `<` | 小于 | BOOL |
| `>` | 大于 | BOOL |
| `<=` | 小于等于 | BOOL |
| `>=` | 大于等于 | BOOL |
| `AND` / `&&` | 逻辑与（短路求值） | BOOL |
| `OR` / `\|\|` | 逻辑或（短路求值） | BOOL |
| `NOT` / `!` | 逻辑非 | BOOL |

### 1.3 字面量

| 类型 | 示例 | 说明 |
|---|---|---|
| 整数 | `42`, `0`, `-1` | 32 位有符号整数 |
| 浮点数 | `3.14`, `0.5`, `-1.0` | 64 位双精度浮点 |
| 布尔 | `true`, `false` | 布尔值 |
| 字符串 | `"hello"`, `"reason"` | 双引号包裹，不支持转义 |

### 1.4 标识符

- 以字母或下划线开头
- 可包含字母、数字、下划线、点号
- 示例: `user.age`, `transaction.amount`, `is_active`

### 1.5 注释

当前版本不支持注释。

---

## 2. 语法规则

### 2.1 简单规则 (WHEN/THEN)

适用于二元通过/不通过场景（如格式校验、黑名单检查）。

```dsl
RULE <规则名> {
    META {
        description = "<规则描述>"
    }
    WHEN <条件表达式>
    THEN <动作名>("<参数>")
}
```

**示例**:
```dsl
RULE check_blacklist {
    META {
        description = "Supplier must not be on blacklist"
    }
    WHEN supplier.is_blacklisted == true
    THEN reject_supplier("blacklisted")
}
```

**执行逻辑**:
- WHEN 条件为 **true** → 触发 THEN 动作 → 状态: **FAILED**
- WHEN 条件为 **false** → 不触发动作 → 状态: **PASSED**

### 2.2 评分规则 (METRIC/CASE/DEFAULT)

适用于多指标量化评分场景（如供应商资质审查、内容安全评分）。

```dsl
RULE <规则名> {
    META {
        description = "<规则描述>"
        pass_threshold = "<通过分数线>"
        partial_threshold = "<部分通过分数线>"
    }
    METRIC <指标名> {
        META {
            description = "<指标描述>"
            weight = "<满分权重>"
            is_critical = "<true/false>"
        }
        CASE <条件表达式> THEN <动作名>("<参数>")
        CASE <条件表达式> THEN <动作名>("<参数>")
        DEFAULT <动作名>("<参数>")
    }
    METRIC <指标名> {
        ...
    }
}
```

**示例**:
```dsl
RULE supplier_audit {
    META {
        description = "Supplier onboarding audit"
        pass_threshold = "80"
        partial_threshold = "60"
    }
    METRIC credit_check {
        META {
            description = "Blacklist compliance"
            weight = "30"
            is_critical = "true"
        }
        CASE supplier.is_blacklisted == false THEN score(30)
        DEFAULT fail_metric(0, "blacklisted")
    }
    METRIC capital_check {
        META {
            description = "Capital threshold"
            weight = "40"
        }
        CASE supplier.capital >= 5000000 THEN score(40)
        CASE supplier.capital >= 1000000 THEN score(20)
        DEFAULT score(0)
    }
}
```

**执行逻辑**:
1. 按 METRIC 定义顺序逐个执行
2. 每个 METRIC 按 CASE 定义顺序匹配
3. 第一个匹配的 CASE 决定得分，后续 CASE 不再评估
4. 无 CASE 匹配时执行 DEFAULT
5. 累计所有 METRIC 得分
6. 根据阈值和红线项判定最终状态

---

## 3. META 块

META 块用于存储规则或指标的元数据，采用 key=value 字符串对格式。

### 3.1 规则级 META

| Key | 类型 | 说明 |
|---|---|---|
| `description` | string | 规则描述 |
| `category` | string | 规则分类 |
| `pass_threshold` | string(int) | 通过分数线（如 "80"） |
| `partial_threshold` | string(int) | 部分通过分数线（如 "60"） |

### 3.2 指标级 META

| Key | 类型 | 说明 |
|---|---|---|
| `description` | string | 指标描述 |
| `weight` | string(int) | 满分权重（如 "40"） |
| `is_critical` | string(bool) | 是否为红线指标（"true"/"false"） |

**注意**: 所有 META 值均为字符串类型，数字和布尔值需用引号包裹。

---

## 4. 内置动作

| 动作 | 参数 | 说明 |
|---|---|---|
| `score(N)` | int | 给当前指标打 N 分 |
| `fail_metric(0, "reason")` | int, string | 指标失败，得 0 分并记录原因 |
| `reject_supplier("reason")` | string | 拒绝供应商 |
| `reject_document("reason")` | string | 拒绝文档 |
| `block_content("reason")` | string | 拦截内容 |
| `record_warning("reason")` | string | 记录警告 |

**扩展**: 宿主程序可通过 `cdsl_vm_register_action()` 注册自定义动作。

---

## 5. 三态判定

### 5.1 简单规则

| WHEN 结果 | 状态 | 分数 |
|---|---|---|
| false | PASSED | 100/100 |
| true | FAILED | 0/100 |

### 5.2 评分规则

```
if (任一 is_critical="true" 指标得分 == 0):
    status = FAILED (一票否决)
else if (总分 >= pass_threshold):
    status = PASSED
else if (总分 >= partial_threshold):
    status = PARTIALLY_PASSED
else:
    status = FAILED
```

### 5.3 报告输出示例

```
========================================
  AUDIT REPORT: supplier_audit
  Supplier onboarding audit
========================================
  [PASS] credit_check (weight: 30, score: 30/30) *
  [PASS] capital_check (weight: 40, score: 20/40)
  [FAIL] experience_check (weight: 30, score: 0/30)
         Reason: short_experience
----------------------------------------
  Status:   PARTIALLY PASSED
  Score:    50 / 100
  Summary:  PARTIALLY PASSED (score: 50/100, needs improvement)
========================================
```

---

## 6. JSON 上下文

通过 `cdsl_context_load_json()` 可以从 JSON 字符串加载上下文变量：

```json
{
    "supplier": {
        "registered_capital": 5000000,
        "years_in_business": 5,
        "is_blacklisted": false
    },
    "document": {
        "format": "pdf",
        "size_mb": 5.0,
        "has_digital_signature": true
    }
}
```

**自动转换规则**:
- JSON number (无小数) → `CDSL_TYPE_INT`
- JSON number (有小数) → `CDSL_TYPE_FLOAT`
- JSON boolean → `CDSL_TYPE_BOOL`
- JSON string → `CDSL_TYPE_STRING`
- JSON object → 递归展开，键名用 `.` 连接

---

## 7. 完整示例

### 7.1 供应商资质审查

```dsl
RULE supplier_qualification {
    META {
        description = "Supplier onboarding qualification audit"
        category = "compliance"
        pass_threshold = "80"
        partial_threshold = "60"
    }
    METRIC credit_check {
        META {
            description = "Blacklist and fraud compliance"
            weight = "30"
            is_critical = "true"
        }
        CASE supplier.is_blacklisted == false THEN score(30)
        DEFAULT fail_metric(0, "blacklisted_rejected")
    }
    METRIC capital_check {
        META {
            description = "Registered capital evaluation"
            weight = "40"
        }
        CASE supplier.registered_capital >= 5000000 THEN score(40)
        CASE supplier.registered_capital >= 1000000 THEN score(20)
        DEFAULT score(0)
    }
    METRIC experience_check {
        META {
            description = "Business age evaluation"
            weight = "30"
        }
        CASE supplier.years_in_business >= 5 THEN score(30)
        CASE supplier.years_in_business >= 2 THEN score(15)
        DEFAULT score(0)
    }
}
```

### 7.2 文档格式审查

```dsl
RULE document_format_audit {
    META {
        description = "Document format and signature compliance"
        pass_threshold = "100"
        partial_threshold = "60"
    }
    METRIC format_check {
        META {
            description = "File format validation"
            weight = "50"
            is_critical = "true"
        }
        CASE document.format == "pdf" THEN score(50)
        DEFAULT fail_metric(0, "invalid_format")
    }
    METRIC signature_check {
        META {
            description = "Digital signature verification"
            weight = "30"
        }
        CASE document.has_digital_signature == true THEN score(30)
        DEFAULT score(0)
    }
    METRIC size_check {
        META {
            description = "File size validation"
            weight = "20"
        }
        CASE document.size_mb <= 10.0 THEN score(20)
        CASE document.size_mb <= 20.0 THEN score(10)
        DEFAULT score(0)
    }
}
```

### 7.3 简单黑名单检查

```dsl
RULE check_blacklist {
    META {
        description = "Supplier must not be on blacklist"
    }
    WHEN supplier.is_blacklisted == true
    THEN reject_supplier("blacklisted")
}
```
