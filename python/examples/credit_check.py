#!/usr/bin/env python3
"""CDSL credit check example — demonstrates Pythonic API usage."""

import cdsl


def main():
    # --- Schema definition (chained API) ---
    schema = (
        cdsl.Schema()
        .add_var("user.age", cdsl.Type.INT)
        .add_var("user.income", cdsl.Type.FLOAT)
        .add_var("user.has_guarantor", cdsl.Type.BOOL)
        .add_action("approve", cdsl.Type.VOID, [])
        .add_action("reject", cdsl.Type.VOID, [cdsl.Type.STRING])
    )
    print(f"Schema: {schema!r}")

    # --- Parse DSL ---
    dsl = """
    RULE credit_check {
        META { description = "Credit approval check"; }
        WHEN user.age >= 18 AND user.income >= 25000.0
        THEN reject("income_too_low")
    }
    """
    rule = cdsl.parse(dsl)
    print(f"Parsed rule: {rule!r}")
    schema.verify(rule)

    # --- Execute ---
    ctx = cdsl.Context(schema)
    ctx.set_int("user.age", 30).set_float("user.income", 50000.0).set_bool(
        "user.has_guarantor", True
    )

    vm = cdsl.VM(schema)
    report = vm.execute(rule, ctx)
    print(report)
    print("JSON:", report.to_json())

    # --- DOT generation ---
    dot = cdsl.to_dot(rule)
    print(f"DOT ({len(dot)} chars): ...{dot[-80:]}")

    # --- Code generation ---
    c_code = cdsl.generate_code(rule, schema)
    print(f"C code ({len(c_code)} chars): ...{c_code[-80:]}")

    # --- Ruleset ---
    ruleset = cdsl.Ruleset()
    ruleset.add(rule, priority=1)
    ruleset_report = ruleset.execute(vm, ctx)
    print(f"Ruleset report: {ruleset_report!r}")

    # --- Cleanup ---
    ctx.free()
    schema.free()
    rule.free()
    vm.free()
    ruleset.free()
    print("All done.")


if __name__ == "__main__":
    main()
