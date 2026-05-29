# DSL Syntax Reference

## 1. Lexical Rules

### 1.1 Keywords

| Keyword | Description |
|---------|-------------|
| `RULE` | Rule definition start |
| `META` | Metadata block start |
| `WHEN` | Condition expression (simple rules) |
| `THEN` | Action declaration (simple rules) |
| `METRIC` | Metric block start (scoring rules) |
| `CASE` | Condition branch (scoring rules) |
| `DEFAULT` | Default branch (scoring rules) |
| `TEMPLATE` | Template rule definition (inheritance) |
| `EXTENDS` | Rule inheritance from template |
| `AND` / `&&` | Logical AND |
| `OR` / `\|\|` | Logical OR |
| `NOT` / `!` | Logical NOT |
| `true` / `false` | Boolean literals |

### 1.2 Operators

| Operator | Description | Return Type |
|----------|-------------|-------------|
| `==` | Equal | BOOL |
| `!=` | Not equal | BOOL |
| `<` | Less than | BOOL |
| `>` | Greater than | BOOL |
| `<=` | Less or equal | BOOL |
| `>=` | Greater or equal | BOOL |
| `AND` / `&&` | Logical AND (short-circuit) | BOOL |
| `OR` / `\|\|` | Logical OR (short-circuit) | BOOL |
| `NOT` / `!` | Logical NOT | BOOL |

### 1.3 Literals

| Type | Examples | Notes |
|------|----------|-------|
| Integer | `42`, `0`, `-1` | 32-bit signed integer |
| Float | `3.14`, `0.5`, `-1.0` | 64-bit double precision |
| Boolean | `true`, `false` | Boolean literals |
| String | `"hello"`, `"reason"` | Double-quoted, no escape support |

### 1.4 Identifiers

- Start with letter or underscore
- Letters, digits, underscores, and dots allowed
- Examples: `user.age`, `transaction.amount`, `is_active`, `strlen`

### 1.5 Functions

Function calls in expressions use `func_name(arg1, arg2, ...)` syntax:
```
strlen(user.name)
abs(transaction.amount)
```

Functions are registered via `cdsl_vm_register_function()` in the host program.

### 1.6 Comments

Comments are not supported in the current grammar.

---

## 2. Grammar Rules

### 2.1 Simple Rule (WHEN/THEN)

For binary pass/fail scenarios (format validation, blacklist checks).

```dsl
RULE <name> {
    META {
        description = "<description>"
    }
    WHEN <condition>
    THEN <action>("<arg>")
}
```

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

**Execution logic**:
- WHEN condition is **true** → trigger THEN action → status: **FAILED**
- WHEN condition is **false** → no action → status: **PASSED**

### 2.2 Scoring Rule (METRIC/CASE/DEFAULT)

For multi-metric quantification scenarios (qualification audits, content scoring).

```dsl
RULE <name> {
    META {
        description = "<description>"
        pass_threshold = "<pass score>"
        partial_threshold = "<partial score>"
    }
    METRIC <name> {
        META {
            description = "<description>"
            weight = "<weight>"
            is_critical = "<true/false>"
        }
        CASE <condition> THEN <action>("<arg>")
        CASE <condition> THEN <action>("<arg>")
        DEFAULT <action>("<arg>")
    }
    METRIC <name> {
        ...
    }
}
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
}
```

**Execution logic**:
1. Process METRICs in declaration order
2. Each METRIC evaluates CASEs in order
3. First matching CASE determines score; remaining CASEs skipped
4. No CASE matched → execute DEFAULT
5. Accumulate all metric scores
6. Apply thresholds and critical-rule veto for final status

### 2.3 Template Rule (TEMPLATE/EXTENDS)

For reusable rule definitions. Templates define metric blocks that can be inherited.

```dsl
TEMPLATE <name> {
    METRIC <name> {
        META { weight = "<weight>" ... }
        CASE <condition> THEN <action>("<arg>")
        DEFAULT <action>("<arg>")
    }
}

RULE <name> EXTENDS <template_name> {
    METRIC <name> {
        ...
    }
}
```

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

**Resolution**:
- Templates are registered globally via `cdsl_register_template()` during parsing
- `EXTENDS` copies the template's metrics into the extending rule
- Custom metrics are appended after inherited ones
- Template names are local to `parser.y` and defined within the same DSL string

---

## 3. META Block

META blocks store key=value string pairs as metadata for rules or metrics.

### 3.1 Rule-level META

| Key | Type | Description |
|-----|------|-------------|
| `description` | string | Rule description |
| `category` | string | Rule category |
| `pass_threshold` | string(int) | Pass threshold (e.g. "80") |
| `partial_threshold` | string(int) | Partial pass threshold (e.g. "60") |
| `depends_on` | string | Comma-separated rule dependencies for RuleSet ordering |

### 3.2 Metric-level META

| Key | Type | Description |
|-----|------|-------------|
| `description` | string | Metric description |
| `weight` | string(int) | Max score weight (e.g. "40") |
| `is_critical` | string(bool) | Critical/veto metric ("true"/"false") |

**Note**: All META values are strings. Numeric and boolean values must be quoted.

---

## 4. Built-in Actions

| Action | Args | Description |
|--------|------|-------------|
| `score(N)` | int | Score N points for current metric |
| `fail_metric(0, "reason")` | int, string | Fail metric with 0 score and reason |
| `reject_supplier("reason")` | string | Reject supplier with reason |
| `reject_document("reason")` | string | Reject document with reason |
| `block_content("reason")` | string | Block content with reason |
| `record_warning("reason")` | string | Record warning with reason |

**Extension**: Host programs can register custom actions via `cdsl_vm_register_action()`.

---

## 5. Tri-state Decision

### 5.1 Simple Rule

| WHEN Result | Status | Score |
|-------------|--------|-------|
| false | PASSED | 100/100 |
| true | FAILED | 0/100 |

### 5.2 Scoring Rule

```
if (any is_critical="true" metric scores 0):
    status = FAILED (veto)
else if (total >= pass_threshold):
    status = PASSED
else if (total >= partial_threshold):
    status = PARTIALLY_PASSED
else:
    status = FAILED
```

### 5.3 Report Output Example

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
  (*) critical metric
```

---

## 6. JSON Context

Variables can be loaded from JSON via `cdsl_context_load_json()`:

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

**Auto-conversion rules**:
- JSON number (no decimal) → `CDSL_TYPE_INT`
- JSON number (with decimal) → `CDSL_TYPE_FLOAT`
- JSON boolean → `CDSL_TYPE_BOOL`
- JSON string → `CDSL_TYPE_STRING`
- JSON object → recursively flattened, keys joined with `.`

---

## 7. Complete Examples

### 7.1 Supplier Qualification Audit

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

### 7.2 Document Format Audit

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

### 7.3 Simple Blacklist Check

```dsl
RULE check_blacklist {
    META {
        description = "Supplier must not be on blacklist"
    }
    WHEN supplier.is_blacklisted == true
    THEN reject_supplier("blacklisted")
}
```

### 7.4 Template Inheritance

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

### 7.5 Custom Function Call

```dsl
RULE check_name_length {
    META { description = "Validate name length" }
    WHEN strlen(user.name) > 0
    THEN record_warning("name_exists")
}
```

Functions like `strlen` must be registered via `cdsl_vm_register_function()` in the host program.
