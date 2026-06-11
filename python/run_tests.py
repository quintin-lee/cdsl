#!/usr/bin/env python3
"""Run CDSL Python binding tests in subprocesses for isolation."""

import subprocess
import sys
import os

test_code = r'''
import sys, os
sys.path.insert(0, 'python')
import cdsl
'''

tests = [
    ("parse_simple", '''
r = cdsl.parse('RULE t { WHEN 1 > 0 THEN a() }')
assert r.name == 't'
'''),
    ("schema_verify", '''
s = cdsl.Schema()
s.add_var('x', cdsl.Type.INT)
s.add_action('a', cdsl.Type.VOID, [])
r = cdsl.parse('RULE t { WHEN x > 0 THEN a() }')
s.verify(r)
'''),
    ("execution", '''
s = cdsl.Schema()
s.add_var('x', cdsl.Type.INT)
s.add_action('a', cdsl.Type.VOID, [])
r = cdsl.parse('RULE t { WHEN x > 0 THEN a() }')
s.verify(r)
ctx = cdsl.Context(s)
ctx.set_int('x', 5)
vm = cdsl.VM(s)
rep = vm.execute(r, ctx)
assert rep.rule_name == 't'
j = rep.to_json()
assert 't' in j
'''),
    ("execution_fail", '''
s = cdsl.Schema()
s.add_var('x', cdsl.Type.INT)
s.add_action('a', cdsl.Type.VOID, [])
r = cdsl.parse('RULE t { WHEN x > 0 THEN a() }')
s.verify(r)
ctx = cdsl.Context(s)
ctx.set_int('x', -1)
vm = cdsl.VM(s)
rep = vm.execute(r, ctx)
assert rep.status == cdsl.Status.PASSED
'''),
    ("parse_error", '''
try:
    cdsl.parse('RULE bad { WHEN x > ] THEN a() }')
    assert False
except cdsl.DSLError:
    pass
'''),
    ("verify_error", '''
s = cdsl.Schema()
s.add_var('x', cdsl.Type.INT)
s.add_action('a', cdsl.Type.VOID, [])
r = cdsl.parse('RULE t { WHEN unknown > 5 THEN a() }')
try:
    s.verify(r)
    assert False
except cdsl.DSLError:
    pass
'''),
    ("context_roundtrip", '''
s = cdsl.Schema()
s.add_var('msg', cdsl.Type.STRING)
s.add_var('n', cdsl.Type.INT)
s.add_var('r', cdsl.Type.FLOAT)
s.add_var('f', cdsl.Type.BOOL)
s.add_action('a', cdsl.Type.VOID, [])
ctx = cdsl.Context(s)
ctx.set_string('msg', 'hi')
ctx.set_int('n', 42)
ctx.set_float('r', 3.14)
ctx.set_bool('f', True)
assert ctx.get_string('msg') == 'hi'
assert ctx.get_int('n') == 42
assert abs(ctx.get_float('r') - 3.14) < 1e-9
assert ctx.get_bool('f') is True
'''),
    ("context_load_json", '''
s = cdsl.Schema()
s.add_var('x', cdsl.Type.FLOAT)
s.add_var('msg', cdsl.Type.STRING)
s.add_action('a', cdsl.Type.VOID, [])
ctx = cdsl.Context(s)
ctx.load_json('{"x": 99, "msg": "test"}')
assert abs(ctx.get_float('x') - 99.0) < 1e-9
assert ctx.get_string('msg') == 'test'
'''),
    ("action_callback", '''
s = cdsl.Schema()
s.add_var('x', cdsl.Type.INT)
s.add_action('my_action', cdsl.Type.VOID, [])
r = cdsl.parse('RULE cb { WHEN x > 0 THEN my_action() }')
s.verify(r)
results = []
def cb(name, args, ud):
    if isinstance(name, bytes): name = name.decode()
    results.append(name)
ctx = cdsl.Context(s)
ctx.set_int('x', 10)
vm = cdsl.VM(s)
vm.register_action('my_action', cb)
vm.execute(r, ctx)
assert results == ['my_action']
'''),
    ("vm_stats", '''
s = cdsl.Schema()
s.add_var('x', cdsl.Type.INT)
s.add_action('a', cdsl.Type.VOID, [])
vm = cdsl.VM(s)
st = vm.get_stats()
assert isinstance(st, dict)
vm.reset_stats()
st2 = vm.get_stats()
assert st2['total_executions'] == 0
'''),
    ("sandbox", '''
s = cdsl.Schema()
s.add_var('x', cdsl.Type.INT)
s.add_action('a', cdsl.Type.VOID, [])
r = cdsl.parse('RULE t { WHEN x > 0 THEN a() }')
s.verify(r)
vm = cdsl.VM(s)
vm.set_timeout(1000000)
vm.set_memory_limit(1024*1024)
vm.set_instruction_limit(1000)
assert vm.get_timeout() == 1000000
assert vm.get_memory_limit() == 1024*1024
assert vm.get_instruction_limit() == 1000
'''),
    ("metric_rule", '''
s = cdsl.Schema()
s.add_var('score', cdsl.Type.INT)
s.add_action('score', cdsl.Type.VOID, [cdsl.Type.INT])
s.add_action('reject', cdsl.Type.VOID, [cdsl.Type.STRING])
r = cdsl.parse('RULE t { METRIC m { META { weight = "50" } CASE score >= 80 THEN score(50) CASE score >= 50 THEN score(30) DEFAULT score(0) } }')
s.verify(r)
ctx = cdsl.Context(s)
ctx.set_int('score', 70)
vm = cdsl.VM(s)
rep = vm.execute(r, ctx)
assert rep.to_json()
'''),
    ("dot_output", '''
s = cdsl.Schema()
s.add_var('x', cdsl.Type.INT)
s.add_action('a', cdsl.Type.VOID, [])
r = cdsl.parse('RULE t { WHEN x > 0 THEN a() }')
dot = cdsl.to_dot(r)
assert 'digraph' in dot
'''),
    ("detailed_errors", '''
s = cdsl.Schema()
s.add_var('x', cdsl.Type.INT)
s.add_action('a', cdsl.Type.VOID, [])
r = cdsl.parse('RULE t { WHEN x > 0 THEN a() }')
errs = s.verify_detailed(r)
assert isinstance(errs, list)
'''),
    ("analysis", '''
s = cdsl.Schema()
s.add_var('x', cdsl.Type.INT)
s.add_action('a', cdsl.Type.VOID, [])
r = cdsl.parse('RULE t { WHEN x >= 0 OR x < 0 THEN a() }')
w = s.analyze(r)
assert isinstance(w, list)
'''),
    ("codegen", '''
s = cdsl.Schema()
s.add_var('x', cdsl.Type.INT)
s.add_action('a', cdsl.Type.VOID, [])
r = cdsl.parse('RULE t { WHEN x > 0 THEN a() }')
s.verify(r)
code = cdsl.generate_code(r, s)
assert 't' in code
'''),
]

def main():
    base_dir = os.path.join(os.path.dirname(__file__), "..")
    env = os.environ.copy()
    env["PYTHONPATH"] = os.path.join(base_dir, "python")

    passed = 0
    failed = 0
    for name, code in tests:
        full = 'import sys, os\nsys.path.insert(0, os.environ["PYTHONPATH"])\nimport cdsl\n' + code
        proc = subprocess.run(
            [sys.executable, "-u", "-c", full],
            capture_output=True, text=True, timeout=10, env=env,
        )
        if proc.returncode == 0:
            print(f"  \u2713 {name}")
            passed += 1
        else:
            print(f"  \u2717 {name}")
            if proc.stderr:
                for line in proc.stderr.strip().splitlines()[-3:]:
                    print(f"       {line}")
            failed += 1

    print(f"\n{'='*40}")
    print(f"Results: {passed} passed, {failed} failed, {len(tests)} total")
    return 0 if failed == 0 else 1

if __name__ == "__main__":
    sys.exit(main())
