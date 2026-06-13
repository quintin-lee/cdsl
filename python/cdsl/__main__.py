"""CLI entry point for cdsl — parse, verify, execute DSL rules.

Usage::

    python -m cdsl parse <file.dsl>
    python -m cdsl verify <file.dsl> <schema.json>
    python -m cdsl run <file.dsl> <data.json>
    python -m cdsl dot <file.dsl>
    python -m cdsl codegen <file.dsl>
"""

import json
import sys
from typing import NoReturn

from cdsl import (DSLError, Context, Ruleset, Schema, Type, VM, parse_file,
                   generate_code, to_dot)


def _die(msg: str) -> NoReturn:
    print(f"error: {msg}", file=sys.stderr)
    sys.exit(1)


def cmd_parse(args: list[str]) -> None:
    """Parse a DSL file and print the rule name."""
    if len(args) != 1:
        _die("Usage: python -m cdsl parse <file.dsl>")
    rule = parse_file(args[0])
    print(rule.name)


def cmd_verify(args: list[str]) -> None:
    """Verify a DSL file against a schema (JSON)."""
    if len(args) != 2:
        _die("Usage: python -m cdsl verify <file.dsl> <schema.json>")
    rule = parse_file(args[0])
    schema = _load_schema(args[1])
    errs = schema.verify_detailed(rule)
    if errs:
        for e in errs:
            print(f"  line {e['line']}: {e['message']}")
        sys.exit(1)
    print("OK")


def cmd_run(args: list[str]) -> None:
    """Run a DSL rule with JSON context data."""
    if len(args) != 2:
        _die("Usage: python -m cdsl run <file.dsl> <data.json>")
    rule = parse_file(args[0])
    with open(args[1]) as f:
        data = json.load(f)
    schema = _infer_schema(rule)
    ctx = Context(schema)
    for k, v in data.items():
        _set_context_value(ctx, k, v)
    vm = VM(schema)
    vm.register_action("approve", lambda n, a, d: None)
    vm.register_action("reject", lambda n, a, d: None)
    vm.register_action("score", lambda n, a, d: None)
    vm.register_action("fail_metric", lambda n, a, d: None)
    report = vm.execute(rule, ctx)
    print(report)
    print(f"  Raw: {report.to_json()}")


def cmd_dot(args: list[str]) -> None:
    """Generate DOT graph from a DSL file."""
    if len(args) != 1:
        _die("Usage: python -m cdsl dot <file.dsl>")
    rule = parse_file(args[0])
    print(to_dot(rule))


def cmd_codegen(args: list[str]) -> None:
    """Generate C code from a DSL file."""
    if len(args) != 2:
        _die("Usage: python -m cdsl codegen <file.dsl> <schema.json>")
    rule = parse_file(args[0])
    schema = _load_schema(args[1])
    print(generate_code(rule, schema))


def _load_schema(path: str) -> Schema:
    with open(path) as f:
        data = json.load(f)
    s = Schema()
    for v in data.get("vars", []):
        s.add_var(v["name"], getattr(Type, v["type"].upper()))
    for a in data.get("actions", []):
        arg_types = [getattr(Type, t.upper()) for t in a.get("args", [])]
        s.add_action(a["name"], getattr(Type, a["return"].upper()), arg_types)
    return s


def _infer_schema(rule) -> Schema:
    schema = Schema()
    for name in ("user.age", "user.income"):
        schema.add_var(name, Type.INT)
    schema.add_action("approve", Type.VOID, [])
    schema.add_action("reject", Type.VOID, [Type.STRING])
    schema.add_action("score", Type.VOID, [Type.INT])
    schema.add_action("fail_metric", Type.VOID, [Type.INT, Type.STRING])
    return schema


def _set_context_value(ctx: Context, key: str, value) -> None:
    if isinstance(value, bool):
        ctx.set_bool(key, value)
    elif isinstance(value, int):
        ctx.set_int(key, value)
    elif isinstance(value, float):
        ctx.set_float(key, value)
    elif isinstance(value, str):
        ctx.set_string(key, value)
    elif value is None:
        pass


def main() -> None:
    if len(sys.argv) < 2 or sys.argv[1] in ("-h", "--help"):
        print(__doc__)
        sys.exit(0)
    cmd = sys.argv[1]
    args = sys.argv[2:]
    commands = {
        "parse": cmd_parse,
        "verify": cmd_verify,
        "run": cmd_run,
        "dot": cmd_dot,
        "codegen": cmd_codegen,
    }
    if cmd not in commands:
        _die(f"Unknown command: {cmd}.  Try -h for help.")
    try:
        commands[cmd](args)
    except DSLError as e:
        _die(str(e))
    except FileNotFoundError as e:
        _die(str(e))


if __name__ == "__main__":
    main()
