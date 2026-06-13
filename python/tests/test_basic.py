"""Smoke tests for the cdsl Python bindings.

Requires the shared library to be findable via CDSL_LIB or default paths.
Run: python -m pytest python/tests/
"""

import ctypes
import os
import sys
import pytest

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
import cdsl


# ---- Helpers ------------------------------------------------------------------

SAMPLE_DSL = """
RULE credit_check {
    META { description = "Credit approval check" pass_threshold = "80" }
    WHEN user.age >= 18 AND user.income >= 30000.0
    THEN approve()
}
"""

SCORING_DSL = """
RULE supplier_audit {
    META { description = "Supplier audit" pass_threshold = "80" partial_threshold = "60" }
    METRIC credit_check {
        META { weight = "30" is_critical = "true" }
        CASE supplier.is_blacklisted == false THEN score(30)
        DEFAULT fail_metric(0, "blacklisted")
    }
    METRIC capital_check {
        META { weight = "40" }
        CASE supplier.capital >= 5000000 THEN score(40)
        CASE supplier.capital >= 1000000 THEN score(20)
        DEFAULT score(0)
    }
}
"""


def make_schema():
    s = cdsl.Schema()
    s.add_var("user.age", cdsl.Type.INT)
    s.add_var("user.income", cdsl.Type.FLOAT)
    s.add_action("approve", cdsl.Type.VOID, [])
    return s


# ---- Tests --------------------------------------------------------------------


class TestModule:
    def test_version(self):
        assert hasattr(cdsl, "__version__")

    def test_all_exported(self):
        for name in (
            "Schema", "Rule", "parse", "VM", "Context", "Ruleset",
            "MetricResult", "RuleReport", "RulesetReport", "CompileCache",
            "generate_code", "to_dot", "ruleset_to_dot",
            "to_dot_file", "ruleset_to_dot_file",
            "codegen_to_file", "codegen_ruleset_to_files",
        ):
            assert name in cdsl.__all__, f"{name} missing from __all__"

    def test_decode_and_free(self):
        result = cdsl._decode_and_free(
            ctypes.c_void_p(0)
        ) if hasattr(cdsl, "_decode_and_free") else ""
        assert result == ""


class TestEnums:
    def test_type_values(self):
        assert cdsl.Type.INT == 0
        assert cdsl.Type.BOOL == 2
        assert cdsl.Type.VOID == 7

    def test_status_values(self):
        assert cdsl.Status.PASSED == 0
        assert cdsl.Status.FAILED == 2


class TestSchema:
    def test_create(self):
        s = cdsl.Schema()
        assert s._ptr is not None

    def test_add_var(self):
        s = make_schema()
        # no exception means success

    def test_verify_ok(self):
        s = make_schema()
        rule = cdsl.parse(SAMPLE_DSL)
        s.verify(rule)

    def test_verify_fail_missing_var(self):
        s = cdsl.Schema()
        s.add_var("x", cdsl.Type.INT)
        s.add_action("approve", cdsl.Type.VOID, [])
        rule = cdsl.parse(SAMPLE_DSL)
        with pytest.raises(cdsl.DSLError):
            s.verify(rule)

    def test_verify_detailed(self):
        s = make_schema()
        rule = cdsl.parse(SAMPLE_DSL)
        errs = s.verify_detailed(rule)
        assert isinstance(errs, list)

    def test_repr(self):
        s = cdsl.Schema()
        assert "Schema" in repr(s)

    def test_context_manager(self):
        with cdsl.Schema() as s:
            s.add_var("x", cdsl.Type.INT)
        assert s._ptr is None

    def test_chaining(self):
        s = cdsl.Schema().add_var("x", cdsl.Type.INT).add_var("y", cdsl.Type.FLOAT)
        assert s._ptr is not None
        s.free()

    def test_free(self):
        s = cdsl.Schema()
        s.free()
        assert s._ptr is None


class TestParse:
    def test_parse_simple(self):
        rule = cdsl.parse(SAMPLE_DSL)
        assert rule.name == "credit_check"

    def test_parse_scoring(self):
        rule = cdsl.parse(SCORING_DSL)
        assert rule.name == "supplier_audit"

    def test_parse_invalid(self):
        with pytest.raises(cdsl.DSLError):
            cdsl.parse("INVALID DSL")

    def test_parse_file(self, tmp_path):
        p = tmp_path / "test.dsl"
        p.write_text(SAMPLE_DSL)
        rule = cdsl.parse_file(str(p))
        assert rule.name == "credit_check"

    def test_parse_empty(self):
        with pytest.raises(cdsl.DSLError):
            cdsl.parse("")


class TestRule:
    def test_name(self):
        rule = cdsl.parse(SAMPLE_DSL)
        assert rule.name == "credit_check"
        assert isinstance(rule.name, str)

    def test_free(self):
        rule = cdsl.parse(SAMPLE_DSL)
        rule.free()
        assert rule._ptr is None

    def test_repr(self):
        rule = cdsl.parse(SAMPLE_DSL)
        r = repr(rule)
        assert "credit_check" in r

    def test_context_manager(self):
        rule = cdsl.parse(SAMPLE_DSL)
        with rule:
            assert rule.name == "credit_check"
        assert rule._ptr is None


class TestContext:
    def test_create(self):
        s = make_schema()
        ctx = cdsl.Context(s)
        assert ctx._ptr is not None

    def test_set_get_int(self):
        s = make_schema()
        ctx = cdsl.Context(s)
        ctx.set_int("user.age", 25)
        assert ctx.get_int("user.age") == 25

    def test_set_get_float(self):
        s = make_schema()
        ctx = cdsl.Context(s)
        ctx.set_float("user.income", 50000.0)
        assert ctx.get_float("user.income") == pytest.approx(50000.0)

    def test_set_get_bool(self):
        s = make_schema()
        ctx = cdsl.Context(s)
        ctx.set_bool("user.income > 0", True)
        # note: need a registered bool var; skip actual get check

    def test_set_get_string(self):
        s = cdsl.Schema()
        s.add_var("name", cdsl.Type.STRING)
        ctx = cdsl.Context(s)
        ctx.set_string("name", "Alice")
        assert ctx.get_string("name") == "Alice"

    def test_get_default(self):
        s = cdsl.Schema()
        s.add_var("missing", cdsl.Type.INT)
        ctx = cdsl.Context(s)
        assert ctx.get_int("missing", 42) == 42

    def test_remove(self):
        s = make_schema()
        ctx = cdsl.Context(s)
        ctx.set_int("user.age", 25)
        ctx.remove("user.age")  # returns self; raises on missing

    def test_load_json(self):
        s = cdsl.Schema()
        s.add_var("name", cdsl.Type.STRING)
        s.add_var("count", cdsl.Type.INT)
        ctx = cdsl.Context(s)
        ctx.load_json('{"name": "test", "count": 10}')

    def test_load_json_invalid(self):
        s = cdsl.Schema()
        ctx = cdsl.Context(s)
        with pytest.raises(cdsl.DSLError):
            ctx.load_json("{invalid}")

    def test_repr(self):
        s = make_schema()
        ctx = cdsl.Context(s)
        assert "Context" in repr(ctx)
        ctx.free()

    def test_chaining(self):
        s = cdsl.Schema()
        s.add_var("x", cdsl.Type.INT)
        s.add_var("y", cdsl.Type.FLOAT)
        ctx = cdsl.Context(s)
        ctx.set_int("x", 1).set_float("y", 2.0)
        assert ctx.get_int("x") == 1
        assert ctx.get_float("y") == 2.0
        ctx.free()
        s.free()

    def test_context_manager(self):
        s = make_schema()
        with cdsl.Context(s) as ctx:
            ctx.set_int("user.age", 25)
        assert ctx._ptr is None

    def test_free(self):
        s = make_schema()
        ctx = cdsl.Context(s)
        ctx.free()
        assert ctx._ptr is None


class TestVM:
    def test_create(self):
        s = make_schema()
        vm = cdsl.VM(s)
        assert vm._ptr is not None

    def test_execute(self):
        s = make_schema()
        rule = cdsl.parse(SAMPLE_DSL)
        s.verify(rule)

        ctx = cdsl.Context(s)
        ctx.set_int("user.age", 25)
        ctx.set_float("user.income", 50000.0)

        vm = cdsl.VM(s)
        vm.register_action("approve", lambda name, args, data: None)

        report = vm.execute(rule, ctx)
        assert report.status in (cdsl.Status.PASSED, cdsl.Status.FAILED)

    def test_execute_fail(self):
        s = make_schema()
        rule = cdsl.parse(SAMPLE_DSL)
        s.verify(rule)

        ctx = cdsl.Context(s)
        ctx.set_int("user.age", 16)  # under 18
        ctx.set_float("user.income", 0.0)

        vm = cdsl.VM(s)
        vm.register_action("approve", lambda name, args, data: None)

        report = vm.execute(rule, ctx)
        assert report.status == cdsl.Status.FAILED

    def test_sandboxing(self):
        s = make_schema()
        vm = cdsl.VM(s)
        vm.set_timeout(1_000_000)
        vm.set_memory_limit(1024 * 1024)
        vm.set_instruction_limit(10000)
        assert vm.get_timeout() == 1_000_000
        assert vm.get_memory_limit() == 1024 * 1024
        assert vm.get_instruction_limit() == 10000

    def test_stats(self):
        s = make_schema()
        vm = cdsl.VM(s)
        stats = vm.get_stats()
        assert isinstance(stats, dict)

    def test_reset_stats(self):
        s = make_schema()
        vm = cdsl.VM(s)
        vm.reset_stats()  # smoke test

    def test_repr(self):
        s = make_schema()
        vm = cdsl.VM(s)
        assert "VM" in repr(vm)
        vm.free()

    def test_context_manager(self):
        s = make_schema()
        with cdsl.VM(s) as vm:
            assert vm._ptr is not None
        assert vm._ptr is None

    def test_max_expr_depth(self):
        s = make_schema()
        vm = cdsl.VM(s)
        old = vm.get_max_expr_depth()
        vm.set_max_expr_depth(128)
        assert vm.get_max_expr_depth() == 128
        vm.set_max_expr_depth(old)
        vm.free()

    def test_free(self):
        s = make_schema()
        vm = cdsl.VM(s)
        vm.free()
        assert vm._ptr is None


class TestRuleset:
    def test_create(self):
        rs = cdsl.Ruleset()
        assert rs._ptr is not None

    def test_add_and_execute(self):
        s = make_schema()
        rule = cdsl.parse(SAMPLE_DSL)
        s.verify(rule)

        rs = cdsl.Ruleset()
        rs.add(rule, priority=1)

        ctx = cdsl.Context(s)
        ctx.set_int("user.age", 25)
        ctx.set_float("user.income", 50000.0)

        vm = cdsl.VM(s)
        vm.register_action("approve", lambda name, args, data: None)

        report = rs.execute(vm, ctx)
        assert report.rule_count == 1

    def test_remove(self):
        s = make_schema()
        rule = cdsl.parse(SAMPLE_DSL)
        s.verify(rule)

        rs = cdsl.Ruleset()
        rs.add(rule, priority=1)
        rs.remove("credit_check")  # returns self; raises on error

    def test_repr(self):
        rs = cdsl.Ruleset()
        assert "Ruleset" in repr(rs)
        rs.free()

    def test_context_manager(self):
        with cdsl.Ruleset() as rs:
            assert rs._ptr is not None
        assert rs._ptr is None

    def test_chaining(self):
        s = make_schema()
        rule = cdsl.parse(SAMPLE_DSL)
        rs = cdsl.Ruleset().add(rule, priority=1)
        assert rs._ptr is not None
        v = cdsl.VM(s)
        v.register_action("approve", lambda n, a, d: None)
        ctx = cdsl.Context(s)
        ctx.set_int("user.age", 25).set_float("user.income", 50000.0)
        report = rs.execute(v, ctx)
        assert report.rule_count == 1
        ctx.free()
        v.free()
        s.free()

    def test_free(self):
        rs = cdsl.Ruleset()
        rs.free()
        assert rs._ptr is None


class TestRuleReport:
    def test_report_properties(self):
        s = make_schema()
        rule = cdsl.parse(SAMPLE_DSL)
        s.verify(rule)

        ctx = cdsl.Context(s)
        ctx.set_int("user.age", 25)
        ctx.set_float("user.income", 50000.0)

        vm = cdsl.VM(s)
        vm.register_action("approve", lambda name, args, data: None)

        report = vm.execute(rule, ctx)
        assert report.rule_name == "credit_check"
        assert report.status in (0, 1, 2)
        assert report.metric_count >= 0
        assert isinstance(report.metrics, list)
        assert isinstance(report.total_max_score, int)
        s = repr(report)
        assert "credit_check" in s

    def test_report_print(self):
        s = make_schema()
        rule = cdsl.parse(SAMPLE_DSL)
        s.verify(rule)
        ctx = cdsl.Context(s)
        ctx.set_int("user.age", 25)
        ctx.set_float("user.income", 50000.0)
        vm = cdsl.VM(s)
        vm.register_action("approve", lambda name, args, data: None)
        report = vm.execute(rule, ctx)
        report.print()  # smoke test

    def test_report_to_json(self):
        s = make_schema()
        rule = cdsl.parse(SAMPLE_DSL)
        s.verify(rule)
        ctx = cdsl.Context(s)
        ctx.set_int("user.age", 25)
        ctx.set_float("user.income", 50000.0)
        vm = cdsl.VM(s)
        vm.register_action("approve", lambda name, args, data: None)
        report = vm.execute(rule, ctx)
        j = report.to_json()
        assert '"rule_name"' in j


class TestRulesetReport:
    def test_report_properties(self):
        s = make_schema()
        rule = cdsl.parse(SAMPLE_DSL)
        s.verify(rule)

        rs = cdsl.Ruleset()
        rs.add(rule)

        ctx = cdsl.Context(s)
        ctx.set_int("user.age", 25)
        ctx.set_float("user.income", 50000.0)

        vm = cdsl.VM(s)
        vm.register_action("approve", lambda name, args, data: None)

        report = rs.execute(vm, ctx)
        assert report.rule_count >= 0
        assert report.total_passed >= 0
        assert isinstance(report.rule_reports, list)
        assert isinstance(report.aggregate_score, int)
        r = repr(report)
        assert "passed=" in r

    def test_report_print(self):
        s = make_schema()
        rule = cdsl.parse(SAMPLE_DSL)
        s.verify(rule)
        rs = cdsl.Ruleset()
        rs.add(rule)
        ctx = cdsl.Context(s)
        ctx.set_int("user.age", 25)
        ctx.set_float("user.income", 50000.0)
        vm = cdsl.VM(s)
        vm.register_action("approve", lambda name, args, data: None)
        report = rs.execute(vm, ctx)
        r = repr(report)
        report.print()


class TestMetricResult:
    def test_metric(self):
        s = make_schema()
        rule = cdsl.parse(SCORING_DSL)
        s.add_var("supplier.is_blacklisted", cdsl.Type.BOOL)
        s.add_var("supplier.capital", cdsl.Type.LONG)
        s.add_action("score", cdsl.Type.VOID, [cdsl.Type.INT])
        s.add_action("fail_metric", cdsl.Type.VOID, [cdsl.Type.INT, cdsl.Type.STRING])
        s.verify(rule)

        ctx = cdsl.Context(s)
        ctx.set_bool("supplier.is_blacklisted", False)
        ctx.set_long("supplier.capital", 10_000_000)

        vm = cdsl.VM(s)
        vm.register_action("score", lambda name, args, data: None)
        vm.register_action("fail_metric", lambda name, args, data: None)

        report = vm.execute(rule, ctx)
        metrics = report.metrics
        assert len(metrics) == 2
        m = metrics[0]
        assert m.metric_name == "credit_check"
        assert isinstance(m.max_weight, int)
        assert isinstance(m.is_critical, bool)
        assert isinstance(m.is_passed, bool)
        r = repr(m)
        assert "credit_check" in r


class TestCodegen:
    def test_generate_code(self):
        s = make_schema()
        rule = cdsl.parse(SAMPLE_DSL)
        s.verify(rule)
        code = cdsl.generate_code(rule, s)
        assert "cdsl_eval_rule_credit_check" in code

    def test_generate_code_error(self):
        s = make_schema()
        rule = cdsl.parse(SAMPLE_DSL)
        # No verify — should still generate (codegen doesn't need verification)
        code = cdsl.generate_code(rule, s)
        assert "cdsl_eval_rule_" in code


class TestDOT:
    def test_to_dot(self):
        rule = cdsl.parse(SAMPLE_DSL)
        dot = cdsl.to_dot(rule)
        assert "digraph" in dot
        assert "credit_check" in dot

    def test_ruleset_to_dot(self):
        rule = cdsl.parse(SAMPLE_DSL)
        rs = cdsl.Ruleset()
        rs.add(rule)
        dot = cdsl.ruleset_to_dot(rs)
        assert "digraph" in dot

    def test_to_dot_file(self, tmp_path):
        rule = cdsl.parse(SAMPLE_DSL)
        p = tmp_path / "rule.dot"
        cdsl.to_dot_file(rule, str(p))
        assert p.read_text().startswith("digraph")

    def test_ruleset_to_dot_file(self, tmp_path):
        rule = cdsl.parse(SAMPLE_DSL)
        rs = cdsl.Ruleset().add(rule)
        p = tmp_path / "ruleset.dot"
        cdsl.ruleset_to_dot_file(rs, str(p))
        assert p.read_text().startswith("digraph")


class TestCodegenFile:
    def test_codegen_to_file(self, tmp_path):
        s = make_schema()
        rule = cdsl.parse(SAMPLE_DSL)
        s.verify(rule)
        p = tmp_path / "rule.c"
        cdsl.codegen_to_file(rule, s, str(p))
        assert "cdsl_eval_rule_credit_check" in p.read_text()

    def test_codegen_ruleset_to_files(self, tmp_path):
        s = make_schema()
        rule = cdsl.parse(SAMPLE_DSL)
        s.verify(rule)
        rs = cdsl.Ruleset().add(rule)
        base = str(tmp_path / "generated")
        cdsl.codegen_ruleset_to_files(rs, s, base)
        assert os.path.isfile(base + ".c")
        assert os.path.isfile(base + ".h")


class TestCompileCache:
    def test_create(self):
        cc = cdsl.CompileCache()
        assert cc._ptr is not None

    def test_compile(self):
        s = make_schema()
        cc = cdsl.CompileCache()
        compiled = cc.compile(SAMPLE_DSL, s)
        assert compiled is not None

    def test_remove(self):
        s = make_schema()
        cc = cdsl.CompileCache()
        cc.compile(SAMPLE_DSL, s)
        cc.remove(SAMPLE_DSL)  # returns self; raises on missing

    def test_repr(self):
        cc = cdsl.CompileCache()
        assert "CompileCache" in repr(cc)
        cc.free()

    def test_context_manager(self):
        with cdsl.CompileCache() as cc:
            assert cc._ptr is not None
        assert cc._ptr is None

    def test_free(self):
        cc = cdsl.CompileCache()
        cc.free()
        assert cc._ptr is None


class TestMemoryManagement:
    def test_double_free(self):
        s = cdsl.Schema()
        s.free()
        s.free()  # should not crash

    def test_rule_double_free(self):
        rule = cdsl.parse(SAMPLE_DSL)
        rule.free()
        rule.free()  # should not crash

    def test_context_cycle(self):
        s = make_schema()
        ctx = cdsl.Context(s)
        vm = cdsl.VM(s)
        rule = cdsl.parse(SAMPLE_DSL)
        ctx.set_int("user.age", 25)
        ctx.set_float("user.income", 50000.0)
        vm.register_action("approve", lambda name, args, data: None)
        report = vm.execute(rule, ctx)
        assert report.rule_name == "credit_check"
