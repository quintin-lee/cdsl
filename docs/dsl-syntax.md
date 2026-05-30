# DSL Syntax Reference

**Revision**: 1.0 &nbsp;·&nbsp; **Audience**: Rule Authors

---

## 1. Lexical Conventions

### 1.1 Keywords

| Keyword          | Description                              |
|------------------|------------------------------------------|
| `RULE`           | Begins a rule definition                 |
| `META`           | Begins a metadata block                  |
| `WHEN`           | Condition expression (simple rules)      |
| `THEN`           | Action declaration (simple rules)        |
| `METRIC`         | Begins a metric block (scoring rules)    |
| `CASE`           | Condition branch (scoring rules)         |
| `DEFAULT`        | Default branch (scoring rules)           |
| `TEMPLATE`       | Defines a reusable metric template       |
| `EXTENDS`        | Inherits metrics from a template         |
| `AND` / `&&`     | Logical AND                              |
| `OR` / `\|\|`   | Logical OR                               |
| `NOT` / `!`      | Logical NOT                              |
| `true` / `false` | Boolean literals                         |

### 1.2 Operators

| Operator         | Description              | Return Type |
|------------------|--------------------------|-------------|
| `==`             | Equal                    | BOOL        |
| `!=`             | Not equal                | BOOL        |
| `<`              | Less than                | BOOL        |
| `>`              | Greater than             | BOOL        |
| `<=`             | Less than or equal       | BOOL        |
| `>=`             | Greater than or equal    | BOOL        |
| `AND` / `&&`     | Logical AND (short-circuit) | BOOL      |
| `OR` / `\|\|`   | Logical OR (short-circuit)  | BOOL      |
| `NOT` / `!`      | Logical NOT              | BOOL        |
| `+`              | Addition                 | INT/FLOAT   |
| `-`              | Subtraction / negation   | INT/FLOAT   |
| `*`              | Multiplication           | INT/FLOAT   |
| `/`              | Division                 | INT/FLOAT   |

### 1.3 Literals

| Type    | Examples                 | Notes                              |
|---------|--------------------------|------------------------------------|
| Integer | `42`, `0`, `-1`          | 32-bit signed integer              |
| Long    | `123L`, `9999999999L`     | 64-bit signed integer (L suffix)   |
| Float   | `3.14`, `0.5`, `-1.0`    | 64-bit double precision            |
| Boolean | `true`, `false`          | Case-sensitive                     |
| String  | `"hello"`, `"reason"`    | Double-quoted; escape not supported|
| Date    | `@2026-05-30`, `@2026-05-30 14:30:00` | ISO 8601 format; `@` prefix |

### 1.4 Identifiers

Identifiers name variables, metrics, rules, and actions.

- Must begin with a letter (`a-z`, `A-Z`) or underscore (`_`)
- May contain letters, digits, underscores, and dots (`.`)
- Dots are used for nested variable access (e.g., `user.age`)

**Valid**: `user.age`, `transaction.amount`, `is_active`, `_internal_id`

**Invalid**: `2fast`, `user-name`, `user.age.`

### 1.5 Function Calls

Function calls use C-style syntax in expressions:

```dsl
strlen(user.name)
abs(transaction.amount)
min(score1, score2)
```

Functions must be registered via `cdsl_vm_register_function()` in the host program.

### 1.6 Comments

Two comment styles are supported:

| Style            | Syntax        | Scope      |
|------------------|---------------|------------|
| Line comment     | `// text`     | To end of line |
| Block comment    | `/* text */`  | May span multiple lines |

**Examples**:

```dsl
// This is a single-line comment
RULE check {              /* Block comment
    META { description = "Test" }   can span multiple lines */
    WHEN x > 0 THEN score(10)
}
```

---

## 2. Rule Definitions

### 2.1 Simple Rule (WHEN / THEN)

Used for binary pass/fail scenarios: format validation, blacklist checks, threshold enforcement.

```
RULE <name> {
    META {
        description = "<description>"
    }
    WHEN <condition>
    THEN <action>(<args>)
}
```

**Execution**:

| WHEN result | Action  | Status |
|-------------|---------|--------|
| `true`      | THEN fires | FAILED |
| `false`     | No action  | PASSED |

**Example**:

```dsl
RULE check_blacklist {
    META {
        description = "Supplier must not be on blacklist"
    }
    WHEN supplier.is_blacklisted == true
    THEN reject_supplier("blacklisted")
}
```

### 2.2 Scoring Rule (METRIC / CASE / DEFAULT)

Used for multi-metric quantitative evaluation: qualification audits, content scoring, compliance checks.

```
RULE <name> {
    META {
        description = "<description>"
        pass_threshold = "<integer>"
        partial_threshold = "<integer>"
    }
    METRIC <name> {
        META {
            description = "<description>"
            weight = "<integer>"
            is_critical = "<true|false>"
        }
        CASE <condition> THEN <action>(<args>)
        CASE <condition> THEN <action>(<args>)
        DEFAULT <action>(<args>)
    }
    METRIC <name> { ... }
}
```

**Execution**:

1. Process METRICs in declaration order
2. Each METRIC evaluates CASE branches in order
3. First matching CASE determines the score and triggers its action; remaining cases are skipped
4. If no CASE matches, the DEFAULT branch executes
5. Scores are accumulated across all metrics
6. If any `is_critical` metric scores 0 → **FAILED** (veto)
7. Otherwise, compare total against thresholds for final status

**Threshold logic**:

```
if (any critical metric failed)   → FAILED
else if (total >= pass_threshold) → PASSED
else if (total >= partial_threshold) → PARTIALLY_PASSED
else → FAILED
```

**Example**:

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
    METRIC experience_check {
        META {
            description = "Business experience"
            weight = "30"
        }
        CASE supplier.years_in_business >= 5 THEN score(30)
        CASE supplier.years_in_business >= 2 THEN score(15)
        DEFAULT score(0)
    }
}
```

### 2.3 Template Rules (TEMPLATE / EXTENDS)

Used for reusable metric definitions. Templates define metric blocks that can be inherited by multiple rules.

```
TEMPLATE <name> {
    METRIC <name> {
        META { ... }
        CASE <condition> THEN <action>(<args>)
        DEFAULT <action>(<args>)
    }
}

RULE <name> EXTENDS <template_name> {
    METRIC <name> {
        ...
    }
}
```

**Resolution**:

1. Templates are registered globally when parsed
2. `EXTENDS` copies all metrics from the named template into the extending rule
3. Metrics defined in the extending rule body are appended after inherited ones
4. Template names are scoped to the parsing session (within the same DSL string or registration sequence)

**Example**:

```dsl
TEMPLATE base_audit {
    METRIC blacklist {
        META { weight = "30" is_critical = "true" }
        CASE supplier.is_blacklisted == false THEN score(30)
        DEFAULT fail_metric(0, "blacklisted")
    }
}

RULE supplier_audit EXTENDS base_audit {
    METRIC capital {
        META { description = "Capital check" weight = "40" }
        CASE supplier.registered_capital >= 5000000 THEN score(40)
        CASE supplier.registered_capital >= 1000000 THEN score(20)
        DEFAULT score(0)
    }
    METRIC experience {
        META { description = "Experience check" weight = "30" }
        CASE supplier.years_in_business >= 5 THEN score(30)
        CASE supplier.years_in_business >= 2 THEN score(15)
        DEFAULT score(0)
    }
}
```

---

## 3. META Block

META blocks store key-value string pairs as metadata for rules and metrics. All values are strings; numeric and boolean values must be quoted.

### 3.1 Rule-level META

| Key                | Type   | Description                                    |
|--------------------|--------|------------------------------------------------|
| `description`      | string | Human-readable rule description                |
| `category`         | string | Rule category for organization                 |
| `pass_threshold`   | string | Minimum score to pass (e.g., `"80"`)           |
| `partial_threshold`| string | Minimum score for partial pass (e.g., `"60"`)  |
| `depends_on`       | string | Comma-separated rule names for ordering        |

### 3.2 Metric-level META

| Key           | Type   | Description                                     |
|---------------|--------|-------------------------------------------------|
| `description` | string | Human-readable metric description               |
| `weight`      | string | Maximum score for this metric (e.g., `"40"`)    |
| `is_critical` | string | `"true"` = veto if metric scores 0              |

---

## 4. Built-in Actions

| Action                                    | Description                          |
|-------------------------------------------|--------------------------------------|
| `score(N)`                                | Score N points for the current metric|
| `fail_metric(0, "reason")`                | Fail metric with reason              |
| `reject_supplier("reason")`               | Reject supplier with explanation     |
| `reject_document("reason")`               | Reject document with explanation     |
| `block_content("reason")`                 | Block content with explanation       |
| `record_warning("reason")`                | Record a warning                     |

> **Note**: Host programs can register custom actions via `cdsl_vm_register_action()`. The actions listed above are examples used in the demo.

---

## 5. JSON Context

Variables can be loaded from JSON strings via `cdsl_context_load_json()`:

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

**Type conversion rules**:

| JSON type          | Target type       | Notes                         |
|--------------------|-------------------|-------------------------------|
| Number (integer)   | `CDSL_TYPE_INT`   | No decimal point              |
| Number (float)     | `CDSL_TYPE_FLOAT` | Has decimal point             |
| Boolean            | `CDSL_TYPE_BOOL`  | `true` / `false`              |
| String             | `CDSL_TYPE_STRING`| Copied internally             |
| Object             | —                 | Recursively flattened (dot notation) |

After loading, variables are accessible in DSL expressions by their dot-separated path (e.g., `supplier.registered_capital`).

---

## 6. Complete Examples

### 6.1 Supplier Qualification Audit

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

### 6.2 Document Format Audit

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

### 6.3 Simple Blacklist Check

```dsl
RULE check_blacklist {
    META {
        description = "Supplier must not be on blacklist"
    }
    WHEN supplier.is_blacklisted == true
    THEN reject_supplier("blacklisted")
}
```

### 6.4 Template Inheritance

```dsl
TEMPLATE base_compliance {
    METRIC blacklist_check {
        META { weight = "30" is_critical = "true" }
        CASE supplier.is_blacklisted == false THEN score(30)
        DEFAULT fail_metric(0, "blacklisted")
    }
}

RULE full_audit EXTENDS base_compliance {
    METRIC capital_check {
        META { description = "Capital" weight = "40" }
        CASE supplier.registered_capital >= 5000000 THEN score(40)
        DEFAULT score(0)
    }
    METRIC experience_check {
        META { description = "Experience" weight = "30" }
        CASE supplier.years_in_business >= 3 THEN score(30)
        DEFAULT score(0)
    }
}
```

### 6.5 Custom Function Call

```dsl
RULE check_name_length {
    META { description = "Validate name length" }
    WHEN strlen(user.name) > 0
    THEN record_warning("name_exists")
}
```

> **Note**: Functions like `strlen` must be registered via `cdsl_vm_register_function()` in the host program before rule execution.
