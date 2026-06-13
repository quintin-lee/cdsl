"""
cdsl — C-DSL Rule Engine Python Bindings

A Pythonic wrapper around the CDSL C library (libcdsl.so) for parsing,
verifying, and executing DSL rule expressions.

Basic usage::

    import cdsl

    # Define a schema
    schema = cdsl.Schema()
    schema.add_var("user.age", cdsl.Type.INT)
    schema.add_var("user.income", cdsl.Type.FLOAT)
    schema.add_action("approve", cdsl.Type.VOID, [])
    schema.add_action("reject", cdsl.Type.VOID, [cdsl.Type.STRING])

    # Parse a rule from DSL
    dsl = '''
    RULE credit_check {
        META { description = "Credit approval check"; }
        WHEN user.age >= 18 AND user.income >= 30000.0
        THEN approve()
    }
    '''
    rule = cdsl.parse(dsl)
    schema.verify(rule)

    # Execute with context
    ctx = cdsl.Context(schema)
    ctx.set_int("user.age", 25)
    ctx.set_float("user.income", 50000.0)

    vm = cdsl.VM(schema)
    report = vm.execute(rule, ctx)
    print(report.status)  # Status.PASSED
"""

from __future__ import annotations

import ctypes
import json
import os
import platform
import sys
from ctypes import (
    CDLL,
    POINTER,
    Structure,
    Union,
    byref,
    c_bool,
    c_char,
    c_char_p,
    c_double,
    c_int,
    c_int32,
    c_int64,
    c_long,
    c_short,
    c_size_t,
    c_ubyte,
    c_uint,
    c_void_p,
    cdll,
)
from typing import Any, Callable, Dict, List, Optional, Protocol

__all__ = [
    "DSLError", "Schema", "Rule", "Context", "VM", "Ruleset",
    "MetricResult", "RuleReport", "RulesetReport", "CompileCache",
    "CompiledRule",
    "Type", "Op", "ExprType", "Status", "TraceKind",
    "parse", "parse_file",
    "generate_code", "codegen_to_file",
    "codegen_ruleset_to_files", "codegen_ruleset_to_h", "codegen_ruleset_to_c",
    "to_dot", "to_dot_file", "ruleset_to_dot", "ruleset_to_dot_file",
]
__version__ = "1.1.0"

# ---- Locate the shared library ------------------------------------------------

def _find_lib() -> str:
    """Locate the CDSL shared library relative to this package or in standard paths."""
    here = os.path.dirname(os.path.abspath(__file__))
    system = platform.system()
    if system == "Darwin":
        libname = "libcdsl.dylib"
    elif system == "Windows":
        libname = "cdsl.dll"
    else:
        libname = "libcdsl.so"
    candidates = [
        os.path.join(here, "..", "..", "build", libname),
        os.path.join(here, "..", "build", libname),
        os.path.join(here, libname),
        os.path.join("/usr/local/lib", libname),
        os.path.join("/usr/lib", libname),
    ]
    env = os.environ.get("CDSL_LIB")
    if env:
        candidates.insert(0, env)
    for p in candidates:
        resolved = os.path.realpath(p)
        if os.path.isfile(resolved):
            return resolved
    raise OSError(
        f"Cannot find {libname}. Set CDSL_LIB env var or install the library."
    )


_lib_path = _find_lib()
_lib = CDLL(_lib_path, use_errno=True)

# ---- C type aliases -----------------------------------------------------------

c_time_t = c_int64  # time_t is 64-bit on modern Linux/glibc

# ---- Enumerations (Python mirrors) --------------------------------------------

class Type:
    """Value types in the CDSL type system."""
    INT = 0
    FLOAT = 1
    BOOL = 2
    STRING = 3
    DATE = 4
    LONG = 5
    ARRAY = 6
    VOID = 7

    _names = {
        0: "INT", 1: "FLOAT", 2: "BOOL", 3: "STRING",
        4: "DATE", 5: "LONG", 6: "ARRAY", 7: "VOID",
    }


class Op:
    """Operators in DSL expressions."""
    EQ = 0
    NE = 1
    LT = 2
    GT = 3
    LE = 4
    GE = 5
    AND = 6
    OR = 7
    NOT = 8
    ADD = 9
    SUB = 10
    MUL = 11
    DIV = 12
    NEG = 13


class ExprType:
    """Expression node types."""
    ID = 0
    INT = 1
    FLOAT = 2
    BOOL = 3
    STRING = 4
    DATE = 5
    LONG = 6
    ARRAY = 7
    BINARY = 8
    UNARY = 9
    CALL = 10


class Status:
    """Rule execution status."""
    PASSED = 0
    PARTIALLY_PASSED = 1
    FAILED = 2
    ERROR = 3

    _names = {
        0: "PASSED",
        1: "PARTIALLY_PASSED",
        2: "FAILED",
        3: "ERROR",
    }


class TraceKind:
    """Trace event kinds."""
    EXPR = 0
    METRIC = 1
    RULE = 2
    ACTION = 3


# ---- ctypes structure definitions --------------------------------------------

class cdsl_arena_block(Structure):
    pass

class cdsl_arena(Structure):
    pass

cdsl_arena_block._fields_ = [
    ("data", c_char_p),
    ("used", c_size_t),
    ("capacity", c_size_t),
    ("next", POINTER(cdsl_arena_block)),
]

cdsl_arena._fields_ = [
    ("blocks", POINTER(cdsl_arena_block)),
    ("block_size", c_size_t),
]


class cdsl_hashmap_entry(Structure):
    pass

class cdsl_hashmap(Structure):
    pass

cdsl_hashmap_entry._fields_ = [
    ("key", c_char_p),
    ("value", c_void_p),
    ("next", POINTER(cdsl_hashmap_entry)),
]

cdsl_hashmap._fields_ = [
    ("buckets", POINTER(POINTER(cdsl_hashmap_entry))),
    ("bucket_count", c_int),
    ("size", c_int),
]


class cdsl_error(Structure):
    _fields_ = [
        ("line", c_int),
        ("column", c_int),
        ("message", c_char_p),
        ("hint", c_char_p),
        ("kind", c_int),
    ]


class cdsl_error_list(Structure):
    _fields_ = [
        ("errors", POINTER(POINTER(cdsl_error))),
        ("count", c_int),
        ("capacity", c_int),
    ]


class cdsl_json_value(Structure):
    pass

class cdsl_json_value_data_obj(Structure):
    _fields_ = [
        ("items", POINTER(cdsl_json_value)),
        ("count", c_int),
    ]

class cdsl_json_value_data_arr(Structure):
    _fields_ = [
        ("items", POINTER(cdsl_json_value)),
        ("count", c_int),
    ]

class cdsl_json_value_union(Union):
    _fields_ = [
        ("bool_val", c_int),
        ("number_val", c_double),
        ("string_val", c_char_p),
        ("object", cdsl_json_value_data_obj),
        ("array", cdsl_json_value_data_arr),
    ]

cdsl_json_value._fields_ = [
    ("key", c_char_p),
    ("type", c_int),
    ("value", cdsl_json_value_union),
    ("next", POINTER(cdsl_json_value)),
]


class cdsl_var_schema(Structure):
    pass

class cdsl_action_schema(Structure):
    pass

class cdsl_schema(Structure):
    pass

cdsl_var_schema._fields_ = [
    ("name", c_char_p),
    ("type", c_int),
    ("is_readonly", c_int),
    ("next", POINTER(cdsl_var_schema)),
]

cdsl_action_schema._fields_ = [
    ("name", c_char_p),
    ("return_type", c_int),
    ("arg_count", c_int),
    ("arg_types", POINTER(c_int)),
    ("next", POINTER(cdsl_action_schema)),
]

cdsl_schema._fields_ = [
    ("vars", POINTER(cdsl_var_schema)),
    ("actions", POINTER(cdsl_action_schema)),
    ("var_map", POINTER(cdsl_hashmap)),
    ("action_map", POINTER(cdsl_hashmap)),
]


class cdsl_arg_node(Structure):
    pass

class cdsl_expr_node(Structure):
    pass

class cdsl_expr_array_data(Structure):
    _fields_ = [("elements", POINTER(cdsl_arg_node))]

class cdsl_expr_binary_data(Structure):
    _fields_ = [
        ("op", c_int),
        ("left", POINTER(cdsl_expr_node)),
        ("right", POINTER(cdsl_expr_node)),
    ]

class cdsl_expr_unary_data(Structure):
    _fields_ = [
        ("op", c_int),
        ("expr", POINTER(cdsl_expr_node)),
    ]

class cdsl_expr_call_data(Structure):
    _fields_ = [
        ("func_name", c_char_p),
        ("args", POINTER(cdsl_arg_node)),
    ]

class cdsl_expr_data(Union):
    _fields_ = [
        ("id_val", c_char_p),
        ("int_val", c_int),
        ("float_val", c_double),
        ("bool_val", c_int),
        ("string_val", c_char_p),
        ("date_val", c_time_t),
        ("long_val", c_int64),
        ("array", cdsl_expr_array_data),
        ("binary", cdsl_expr_binary_data),
        ("unary", cdsl_expr_unary_data),
        ("call", cdsl_expr_call_data),
    ]

cdsl_expr_node._fields_ = [
    ("type", c_int),
    ("data", cdsl_expr_data),
]

cdsl_arg_node._fields_ = [
    ("expr", POINTER(cdsl_expr_node)),
    ("next", POINTER(cdsl_arg_node)),
]


class cdsl_action_node(Structure):
    _fields_ = [
        ("action_name", c_char_p),
        ("args", POINTER(cdsl_arg_node)),
    ]


class cdsl_meta_item(Structure):
    pass

cdsl_meta_item._fields_ = [
    ("key", c_char_p),
    ("value", c_char_p),
    ("next", POINTER(cdsl_meta_item)),
]


class cdsl_case_node(Structure):
    pass

cdsl_case_node._fields_ = [
    ("condition", POINTER(cdsl_expr_node)),
    ("action", POINTER(cdsl_action_node)),
    ("next", POINTER(cdsl_case_node)),
]


class cdsl_metric_node(Structure):
    pass

cdsl_metric_node._fields_ = [
    ("name", c_char_p),
    ("meta_list", POINTER(cdsl_meta_item)),
    ("case_list", POINTER(cdsl_case_node)),
    ("default_action", POINTER(cdsl_action_node)),
    ("next", POINTER(cdsl_metric_node)),
]


class cdsl_rule(Structure):
    _fields_ = [
        ("name", c_char_p),
        ("meta_list", POINTER(cdsl_meta_item)),
        ("when_expr", POINTER(cdsl_expr_node)),
        ("then_action", POINTER(cdsl_action_node)),
        ("metrics", POINTER(cdsl_metric_node)),
        ("arena", POINTER(cdsl_arena)),
    ]


class cdsl_context_entry(Structure):
    pass

class cdsl_array_val(Structure):
    _fields_ = [
        ("items", c_void_p),  # cdsl_value_t*
        ("count", c_int),
        ("capacity", c_int),
    ]

class cdsl_value_data(Union):
    _fields_ = [
        ("int_val", c_int),
        ("float_val", c_double),
        ("bool_val", c_int),
        ("string_val", c_char_p),
        ("date_val", c_time_t),
        ("long_val", c_int64),
        ("array_val", POINTER(cdsl_array_val)),
    ]

class cdsl_value(Structure):
    _fields_ = [
        ("type", c_int),
        ("data", cdsl_value_data),
    ]

cdsl_context_entry._fields_ = [
    ("name", c_char_p),
    ("value", cdsl_value),
    ("next", POINTER(cdsl_context_entry)),
]

class cdsl_context(Structure):
    _fields_ = [
        ("schema", POINTER(cdsl_schema)),
        ("entries", POINTER(cdsl_context_entry)),
        ("map", POINTER(cdsl_hashmap)),
    ]


class cdsl_action_cb_entry(Structure):
    pass

cdsl_action_cb_entry._fields_ = [
    ("action_name", c_char_p),
    ("cb", c_void_p),
    ("next", POINTER(cdsl_action_cb_entry)),
]

class cdsl_func_entry(Structure):
    pass

cdsl_func_entry._fields_ = [
    ("func_name", c_char_p),
    ("cb", c_void_p),
    ("next", POINTER(cdsl_func_entry)),
]

class cdsl_stats(Structure):
    _fields_ = [
        ("total_executions", c_long),
        ("total_rules_executed", c_long),
        ("total_metrics_evaluated", c_long),
        ("total_actions_triggered", c_long),
        ("total_time_us", c_double),
        ("avg_time_us", c_double),
    ]

class cdsl_trace_event(Structure):
    _fields_ = [
        ("kind", c_int),
        ("rule_name", c_char_p),
        ("detail", c_char_p),
        ("value", cdsl_value),
        ("depth", c_int),
        ("timestamp_us", c_double),
    ]

class cdsl_vm(Structure):
    _fields_ = [
        ("schema", POINTER(cdsl_schema)),
        ("callbacks", POINTER(cdsl_action_cb_entry)),
        ("functions", POINTER(cdsl_func_entry)),
        ("user_data", c_void_p),
        ("debug_enabled", c_int),
        ("stats", cdsl_stats),
        ("max_expr_depth", c_int),
        ("timeout_us", c_int64),
        ("memory_limit", c_int64),
        ("instruction_limit", c_int64),
        ("alloc_bytes", c_int64),
        ("instruction_count", c_int64),
        ("error_state", c_int),
        ("trace_cb", c_void_p),
        ("trace_ud", c_void_p),
    ]


class cdsl_metric_result(Structure):
    _fields_ = [
        ("metric_name", c_char_p),
        ("description", c_char_p),
        ("max_weight", c_int),
        ("score_obtained", c_int),
        ("is_critical", c_int),
        ("is_passed", c_int),
        ("matched_case_expr", c_char_p),
        ("violation_reason", c_char_p),
    ]


class cdsl_rule_report(Structure):
    _fields_ = [
        ("rule_name", c_char_p),
        ("description", c_char_p),
        ("metrics", POINTER(cdsl_metric_result)),
        ("metric_count", c_int),
        ("total_max_score", c_int),
        ("total_obtained_score", c_int),
        ("status", c_int),
        ("decision_summary", c_char_p),
    ]


class cdsl_ruleset_entry(Structure):
    pass

cdsl_ruleset_entry._fields_ = [
    ("rule", POINTER(cdsl_rule)),
    ("priority", c_int),
    ("next", POINTER(cdsl_ruleset_entry)),
]

class cdsl_ruleset(Structure):
    _fields_ = [
        ("entries", POINTER(cdsl_ruleset_entry)),
        ("count", c_int),
    ]


class cdsl_ruleset_report(Structure):
    _fields_ = [
        ("rule_reports", POINTER(POINTER(cdsl_rule_report))),
        ("rule_count", c_int),
        ("total_passed", c_int),
        ("total_partially", c_int),
        ("total_failed", c_int),
        ("total_error", c_int),
        ("aggregate_score", c_int),
        ("aggregate_max", c_int),
        ("summary", c_char_p),
    ]


class cdsl_compiled_rule(Structure):
    pass

cdsl_compiled_rule._fields_ = [
    ("rule", POINTER(cdsl_rule)),
    ("dsl_hash", c_char_p),
    ("verified", c_int),
    # bc field omitted — bytecode.h struct not fully needed for Python
]

class cdsl_compile_cache(Structure):
    _fields_ = [
        ("map", POINTER(cdsl_hashmap)),
        ("lock", c_void_p),  # pthread_rwlock_t
    ]


# Bytecode structures
class bc_operand(Union):
    _fields_ = [
        ("int_val", c_int),
        ("float_val", c_double),
        ("bool_val", c_int),
        ("const_idx", c_int),
        ("date_val", c_int64),
        ("long_val", c_int64),
        ("jump_offset", c_int),
    ]

class bc_inst(Structure):
    _fields_ = [
        ("op", c_int),
        ("operand", bc_operand),
    ]


class cdsl_bytecode(Structure):
    _fields_ = [
        ("code", POINTER(bc_inst)),
        ("count", c_int),
        ("capacity", c_int),
        ("max_stack", c_int),
        ("constants", POINTER(cdsl_value)),
        ("const_count", c_int),
        ("const_capacity", c_int),
    ]


# Bytecode opcode names for disassembly
_BC_OP_NAMES = {
    0: "PUSH_INT", 1: "PUSH_FLOAT", 2: "PUSH_BOOL", 3: "PUSH_STRING",
    4: "PUSH_DATE", 5: "PUSH_LONG", 6: "PUSH_ARRAY", 7: "PUSH_VAR",
    8: "POP", 9: "ADD", 10: "SUB", 11: "MUL", 12: "DIV",
    13: "EQ", 14: "NE", 15: "LT", 16: "GT", 17: "LE", 18: "GE",
    19: "NOT", 20: "NEG", 21: "CALL", 22: "DUP",
    23: "JMP_IF_FALSE", 24: "JMP_IF_TRUE", 25: "JMP",
    26: "METRIC_START", 27: "SET_SCORE", 28: "FAIL_METRIC",
    29: "RULE_END", 30: "RET",
}


# Callback types (CFUNCTYPE)
cdsl_action_cb_type = ctypes.CFUNCTYPE(
    None, c_char_p, POINTER(cdsl_arg_node), c_void_p,
)
cdsl_func_cb_type = ctypes.CFUNCTYPE(
    cdsl_value, c_char_p, POINTER(cdsl_arg_node),
    POINTER(cdsl_context), POINTER(cdsl_vm),
)
cdsl_trace_cb_type = ctypes.CFUNCTYPE(
    None, POINTER(cdsl_trace_event), c_void_p,
)

# ---- C function signatures ----------------------------------------------------

_lib.cdsl_schema_create.restype = POINTER(cdsl_schema)

_lib.cdsl_schema_free.argtypes = [POINTER(cdsl_schema)]
_lib.cdsl_schema_free.restype = None

_lib.cdsl_schema_register_var.argtypes = [
    POINTER(cdsl_schema), c_char_p, c_int,
]
_lib.cdsl_schema_register_var.restype = None

_lib.cdsl_schema_register_var_rw.argtypes = [
    POINTER(cdsl_schema), c_char_p, c_int, c_int,
]
_lib.cdsl_schema_register_var_rw.restype = None

_lib.cdsl_schema_register_action.argtypes = [
    POINTER(cdsl_schema), c_char_p, c_int, c_int,
]
_lib.cdsl_schema_register_action.restype = None

_lib.cdsl_verify_rule.argtypes = [
    POINTER(cdsl_rule), POINTER(cdsl_schema), c_char_p, c_int,
]
_lib.cdsl_verify_rule.restype = c_bool

_lib.cdsl_verify_rule_detailed.argtypes = [
    POINTER(cdsl_rule), POINTER(cdsl_schema),
]
_lib.cdsl_verify_rule_detailed.restype = POINTER(cdsl_error_list)

_lib.cdsl_analyze_rule.argtypes = [
    POINTER(cdsl_rule), POINTER(cdsl_schema),
]
_lib.cdsl_analyze_rule.restype = POINTER(cdsl_error_list)

_lib.cdsl_parse_string.argtypes = [c_char_p, POINTER(POINTER(cdsl_error_list))]
_lib.cdsl_parse_string.restype = POINTER(cdsl_rule)

_lib.cdsl_free_rule.argtypes = [POINTER(cdsl_rule)]
_lib.cdsl_free_rule.restype = None

_lib.cdsl_context_create.argtypes = [POINTER(cdsl_schema)]
_lib.cdsl_context_create.restype = POINTER(cdsl_context)

_lib.cdsl_context_free.argtypes = [POINTER(cdsl_context)]
_lib.cdsl_context_free.restype = None

_lib.cdsl_context_set_int.argtypes = [
    POINTER(cdsl_context), c_char_p, c_int,
]
_lib.cdsl_context_set_float.argtypes = [
    POINTER(cdsl_context), c_char_p, c_double,
]
_lib.cdsl_context_set_bool.argtypes = [
    POINTER(cdsl_context), c_char_p, c_int,
]
_lib.cdsl_context_set_string.argtypes = [
    POINTER(cdsl_context), c_char_p, c_char_p,
]
_lib.cdsl_context_set_date.argtypes = [
    POINTER(cdsl_context), c_char_p, c_time_t,
]
_lib.cdsl_context_set_long.argtypes = [
    POINTER(cdsl_context), c_char_p, c_int64,
]

_lib.cdsl_context_get_int.argtypes = [
    POINTER(cdsl_context), c_char_p, c_int,
]
_lib.cdsl_context_get_int.restype = c_int

_lib.cdsl_context_get_float.argtypes = [
    POINTER(cdsl_context), c_char_p, c_double,
]
_lib.cdsl_context_get_float.restype = c_double

_lib.cdsl_context_get_bool.argtypes = [
    POINTER(cdsl_context), c_char_p, c_int,
]
_lib.cdsl_context_get_bool.restype = c_int

_lib.cdsl_context_get_string.argtypes = [
    POINTER(cdsl_context), c_char_p, c_char_p,
]
_lib.cdsl_context_get_string.restype = c_char_p

_lib.cdsl_context_get_date.argtypes = [
    POINTER(cdsl_context), c_char_p, c_time_t,
]
_lib.cdsl_context_get_date.restype = c_time_t

_lib.cdsl_context_get_long.argtypes = [
    POINTER(cdsl_context), c_char_p, c_int64,
]
_lib.cdsl_context_get_long.restype = c_int64

_lib.cdsl_context_remove.argtypes = [
    POINTER(cdsl_context), c_char_p,
]
_lib.cdsl_context_remove.restype = c_int

_lib.cdsl_context_load_json.argtypes = [
    POINTER(cdsl_context), c_char_p,
]
_lib.cdsl_context_load_json.restype = c_int

_lib.cdsl_vm_create.argtypes = [POINTER(cdsl_schema)]
_lib.cdsl_vm_create.restype = POINTER(cdsl_vm)

_lib.cdsl_vm_free.argtypes = [POINTER(cdsl_vm)]
_lib.cdsl_vm_free.restype = None

_lib.cdsl_vm_register_action.argtypes = [
    POINTER(cdsl_vm), c_char_p, cdsl_action_cb_type,
]
_lib.cdsl_vm_register_action.restype = None

_lib.cdsl_vm_register_function.argtypes = [
    POINTER(cdsl_vm), c_char_p, cdsl_func_cb_type,
]
_lib.cdsl_vm_register_function.restype = None

_lib.cdsl_vm_set_debug.argtypes = [POINTER(cdsl_vm), c_int]
_lib.cdsl_vm_set_debug.restype = None

_lib.cdsl_vm_set_timeout.argtypes = [POINTER(cdsl_vm), c_int64]
_lib.cdsl_vm_set_timeout.restype = None

_lib.cdsl_vm_get_timeout.argtypes = [POINTER(cdsl_vm)]
_lib.cdsl_vm_get_timeout.restype = c_int64

_lib.cdsl_vm_set_memory_limit.argtypes = [POINTER(cdsl_vm), c_int64]
_lib.cdsl_vm_set_memory_limit.restype = None

_lib.cdsl_vm_get_memory_limit.argtypes = [POINTER(cdsl_vm)]
_lib.cdsl_vm_get_memory_limit.restype = c_int64

_lib.cdsl_vm_set_instruction_limit.argtypes = [POINTER(cdsl_vm), c_int64]
_lib.cdsl_vm_set_instruction_limit.restype = None

_lib.cdsl_vm_get_instruction_limit.argtypes = [POINTER(cdsl_vm)]
_lib.cdsl_vm_get_instruction_limit.restype = c_int64

_lib.cdsl_vm_set_trace_callback.argtypes = [
    POINTER(cdsl_vm), cdsl_trace_cb_type, c_void_p,
]
_lib.cdsl_vm_set_trace_callback.restype = None

_lib.cdsl_vm_get_stats.argtypes = [POINTER(cdsl_vm)]
_lib.cdsl_vm_get_stats.restype = POINTER(cdsl_stats)

_lib.cdsl_vm_reset_stats.argtypes = [POINTER(cdsl_vm)]
_lib.cdsl_vm_reset_stats.restype = None

_lib.cdsl_vm_execute.argtypes = [
    POINTER(cdsl_vm), POINTER(cdsl_rule), POINTER(cdsl_context),
]
_lib.cdsl_vm_execute.restype = POINTER(cdsl_rule_report)

_lib.cdsl_report_free.argtypes = [POINTER(cdsl_rule_report)]
_lib.cdsl_report_free.restype = None

_lib.cdsl_report_print.argtypes = [POINTER(cdsl_rule_report)]
_lib.cdsl_report_print.restype = None

_lib.cdsl_report_to_json.argtypes = [POINTER(cdsl_rule_report)]
_lib.cdsl_report_to_json.restype = c_void_p

_lib.cdsl_codegen_rule_to_c.argtypes = [
    POINTER(cdsl_rule), POINTER(cdsl_schema),
]
_lib.cdsl_codegen_rule_to_c.restype = c_void_p

_lib.cdsl_codegen_ruleset_to_h.argtypes = [
    POINTER(cdsl_ruleset), POINTER(cdsl_schema), c_char_p,
]
_lib.cdsl_codegen_ruleset_to_h.restype = c_void_p

_lib.cdsl_codegen_ruleset_to_c.argtypes = [
    POINTER(cdsl_ruleset), POINTER(cdsl_schema), c_char_p,
]
_lib.cdsl_codegen_ruleset_to_c.restype = c_void_p

_lib.cdsl_rule_to_dot.argtypes = [POINTER(cdsl_rule)]
_lib.cdsl_rule_to_dot.restype = c_void_p

_lib.cdsl_ruleset_to_dot.argtypes = [POINTER(cdsl_ruleset)]
_lib.cdsl_ruleset_to_dot.restype = c_void_p

_lib.cdsl_rule_to_dot_file.argtypes = [
    POINTER(cdsl_rule), c_char_p,
]
_lib.cdsl_rule_to_dot_file.restype = c_int

_lib.cdsl_ruleset_to_dot_file.argtypes = [
    POINTER(cdsl_ruleset), c_char_p,
]
_lib.cdsl_ruleset_to_dot_file.restype = c_int

# Compile cache
_lib.cdsl_compile.argtypes = [
    POINTER(cdsl_compile_cache), c_char_p,
    POINTER(cdsl_schema), c_char_p, c_int,
]
_lib.cdsl_compile.restype = POINTER(cdsl_compiled_rule)

_lib.cdsl_compile_cache_create.argtypes = [c_int]
_lib.cdsl_compile_cache_create.restype = POINTER(cdsl_compile_cache)

_lib.cdsl_compile_cache_free.argtypes = [POINTER(cdsl_compile_cache)]
_lib.cdsl_compile_cache_free.restype = None

_lib.cdsl_compile_cache_remove.argtypes = [
    POINTER(cdsl_compile_cache), c_char_p,
]
_lib.cdsl_compile_cache_remove.restype = c_int

# Bytecode

# libc free for freeing malloc'd strings from API functions
_lib.free.argtypes = [c_void_p]
_lib.free.restype = None
_lib.cdsl_bytecode_compile.argtypes = [
    POINTER(cdsl_rule), POINTER(cdsl_schema), c_void_p,
]
_lib.cdsl_bytecode_compile.restype = c_int

_lib.cdsl_bytecode_execute_rule.argtypes = [
    POINTER(cdsl_vm), c_void_p, POINTER(cdsl_context),
]
_lib.cdsl_bytecode_execute_rule.restype = POINTER(cdsl_rule_report)

_lib.cdsl_bytecode_free.argtypes = [c_void_p]
_lib.cdsl_bytecode_free.restype = None

# Ruleset
_lib.cdsl_ruleset_create.argtypes = []
_lib.cdsl_ruleset_create.restype = POINTER(cdsl_ruleset)

_lib.cdsl_ruleset_free.argtypes = [POINTER(cdsl_ruleset)]
_lib.cdsl_ruleset_free.restype = None

_lib.cdsl_ruleset_add.argtypes = [
    POINTER(cdsl_ruleset), POINTER(cdsl_rule), c_int,
]
_lib.cdsl_ruleset_add.restype = None

_lib.cdsl_ruleset_remove.argtypes = [POINTER(cdsl_ruleset), c_char_p]
_lib.cdsl_ruleset_remove.restype = c_int

_lib.cdsl_ruleset_load_file.argtypes = [
    POINTER(cdsl_ruleset), c_char_p, c_int,
    POINTER(cdsl_schema), c_char_p, c_int,
]
_lib.cdsl_ruleset_load_file.restype = c_int

_lib.cdsl_ruleset_load_string.argtypes = [
    POINTER(cdsl_ruleset), c_char_p, c_int,
    POINTER(cdsl_schema), c_char_p, c_int,
]
_lib.cdsl_ruleset_load_string.restype = c_int

_lib.cdsl_ruleset_reload_file.argtypes = [
    POINTER(cdsl_ruleset), c_char_p,
    POINTER(cdsl_schema), c_char_p, c_int,
]
_lib.cdsl_ruleset_reload_file.restype = c_int

_lib.cdsl_ruleset_validate_deps.argtypes = [
    POINTER(cdsl_ruleset), c_char_p, c_int,
]
_lib.cdsl_ruleset_validate_deps.restype = c_int

_lib.cdsl_ruleset_topo_sort.argtypes = [POINTER(cdsl_ruleset)]
_lib.cdsl_ruleset_topo_sort.restype = c_int

_lib.cdsl_ruleset_report_free.argtypes = [POINTER(cdsl_ruleset_report)]
_lib.cdsl_ruleset_report_free.restype = None

_lib.cdsl_ruleset_report_print.argtypes = [POINTER(cdsl_ruleset_report)]
_lib.cdsl_ruleset_report_print.restype = None

_lib.cdsl_error_list_free.argtypes = [POINTER(cdsl_error_list)]
_lib.cdsl_error_list_free.restype = None

# VM extended
_lib.cdsl_vm_get_max_expr_depth.argtypes = [POINTER(cdsl_vm)]
_lib.cdsl_vm_get_max_expr_depth.restype = c_int

_lib.cdsl_vm_set_max_expr_depth.argtypes = [POINTER(cdsl_vm), c_int]
_lib.cdsl_vm_set_max_expr_depth.restype = None

_lib.cdsl_vm_execute_ruleset.argtypes = [
    POINTER(cdsl_vm), POINTER(cdsl_ruleset), POINTER(cdsl_context),
]
_lib.cdsl_vm_execute_ruleset.restype = POINTER(cdsl_ruleset_report)

_lib.cdsl_vm_execute_ruleset_parallel.argtypes = [
    POINTER(cdsl_vm), POINTER(cdsl_ruleset), POINTER(cdsl_context), c_int,
]
_lib.cdsl_vm_execute_ruleset_parallel.restype = POINTER(cdsl_ruleset_report)

_lib.cdsl_vm_execute_compiled.argtypes = [
    POINTER(cdsl_vm), c_void_p, POINTER(cdsl_context),
]
_lib.cdsl_vm_execute_compiled.restype = POINTER(cdsl_rule_report)

# Codegen to file
_lib.cdsl_codegen_to_file.argtypes = [
    POINTER(cdsl_rule), POINTER(cdsl_schema), c_char_p,
]
_lib.cdsl_codegen_to_file.restype = c_int

_lib.cdsl_codegen_ruleset_to_files.argtypes = [
    POINTER(cdsl_ruleset), POINTER(cdsl_schema), c_char_p,
]
_lib.cdsl_codegen_ruleset_to_files.restype = c_int


# ---- Pythonic wrapper API -----------------------------------------------------

class DSLError(Exception):
    """Raised for parse/verify/analysis errors.

    Attributes:
        message:  Human-readable error description.
        errors:   List of dicts with keys ``line``, ``column``, ``message``,
                  ``hint``, ``kind`` (empty for non-parse errors).
    """

    def __init__(self, message: str, errors: Optional[List[Dict[str, Any]]] = None):
        super().__init__(message)
        self.message = message
        self.errors = errors or []

    def __str__(self) -> str:
        return self.message

    def __repr__(self) -> str:
        return f"DSLError({self.message}, errors={self.errors})"


class _RuleLike(Protocol):
    """Protocol for objects wrapping a cdsl_rule_t*."""
    @property
    def ptr(self): ...


def _decode_and_free(ptr) -> str:
    """Decode a malloc'd C string (c_void_p) and free it."""
    if not ptr:
        return ""
    p = ctypes.cast(ptr, c_char_p)
    result = p.value.decode("utf-8", errors="replace")
    _lib.free(ptr)
    return result


class Schema:
    """CDSL schema: defines variables and actions for a rule set."""

    def __init__(self) -> None:
        self._ptr = _lib.cdsl_schema_create()
        if not self._ptr:
            raise MemoryError("cdsl_schema_create() returned NULL")
        self._owned = True

    @classmethod
    def _from_ptr(cls, ptr) -> Schema:
        inst = cls.__new__(cls)
        inst._ptr = ptr
        inst._owned = True
        return inst

    def free(self) -> None:
        if self._owned and self._ptr:
            _lib.cdsl_schema_free(self._ptr)
            self._ptr = None
            self._owned = False

    def __del__(self) -> None:
        self.free()

    def __repr__(self) -> str:
        return f"Schema(ptr={self._ptr})"

    def __enter__(self) -> Schema:
        return self

    def __exit__(self, *args) -> None:
        self.free()

    def add_var(
        self, name: str, typ: int, *, readonly: bool = False
    ) -> Schema:
        name_b = name.encode("utf-8")
        if readonly:
            _lib.cdsl_schema_register_var_rw(
                self._ptr, name_b, typ, 1
            )
        else:
            _lib.cdsl_schema_register_var(self._ptr, name_b, typ)
        return self

    def add_action(
        self,
        name: str,
        return_type: int,
        arg_types: List[int],
    ) -> Schema:
        name_b = name.encode("utf-8")
        n = len(arg_types)
        if n == 0:
            _lib.cdsl_schema_register_action(self._ptr, name_b, return_type, 0)
        elif n == 1:
            _lib.cdsl_schema_register_action(
                self._ptr, name_b, return_type, 1, arg_types[0],
            )
        elif n == 2:
            _lib.cdsl_schema_register_action(
                self._ptr, name_b, return_type, 2, arg_types[0], arg_types[1],
            )
        elif n == 3:
            _lib.cdsl_schema_register_action(
                self._ptr, name_b, return_type, 3,
                arg_types[0], arg_types[1], arg_types[2],
            )
        else:
            raise ValueError("Too many action arg types (max 3)")
        return self

    def verify(self, rule: _RuleLike) -> None:
        """Verify a rule against this schema. Raises DSLError on failure."""
        errs = self.verify_detailed(rule)
        if errs:
            message = "Schema verification failed:\n" + "\n".join(
                f"  line {e['line']}: {e['message']}"
                + (f" ({e['hint']})" if e['hint'] else "")
                for e in errs
            )
            raise DSLError(message, errors=errs)

    def verify_detailed(self, rule: _RuleLike) -> List[Dict[str, Any]]:
        errs = _lib.cdsl_verify_rule_detailed(rule.ptr, self._ptr)
        return _collect_errors(errs)

    def analyze(self, rule: _RuleLike) -> List[Dict[str, Any]]:
        errs = _lib.cdsl_analyze_rule(rule.ptr, self._ptr)
        return _collect_errors(errs)

    def list_vars(self) -> List[Dict[str, Any]]:
        """Return a list of registered variables."""
        result: List[Dict[str, Any]] = []
        if not self._ptr:
            return result
        v = self._ptr.contents.vars
        while v:
            result.append({
                "name": _decode(v.contents.name),
                "type": v.contents.type,
                "readonly": bool(v.contents.is_readonly),
            })
            v = v.contents.next
        return result

    def list_actions(self) -> List[Dict[str, Any]]:
        """Return a list of registered actions."""
        result: List[Dict[str, Any]] = []
        if not self._ptr:
            return result
        a = self._ptr.contents.actions
        while a:
            arg_types = [a.contents.arg_types[i] for i in range(a.contents.arg_count)]
            result.append({
                "name": _decode(a.contents.name),
                "return_type": a.contents.return_type,
                "arg_types": arg_types,
            })
            a = a.contents.next
        return result

    def has_var(self, name: str) -> bool:
        return any(v["name"] == name for v in self.list_vars())

    def has_action(self, name: str) -> bool:
        return any(a["name"] == name for a in self.list_actions())

    def var_count(self) -> int:
        count = 0
        if self._ptr:
            v = self._ptr.contents.vars
            while v:
                count += 1
                v = v.contents.next
        return count

    def action_count(self) -> int:
        count = 0
        if self._ptr:
            a = self._ptr.contents.actions
            while a:
                count += 1
                a = a.contents.next
        return count

    def get_var_type(self, name: str) -> Optional[int]:
        for v in self.list_vars():
            if v["name"] == name:
                return v["type"]
        return None

    def __len__(self) -> int:
        return self.var_count()

    def __bool__(self) -> bool:
        return self._ptr is not None


class Rule(_RuleLike):
    """Parsed DSL rule AST."""

    def __init__(self, ptr, owned: bool = True) -> None:
        self._ptr = ptr
        self._owned = owned

    def free(self) -> None:
        if self._owned and self._ptr:
            _lib.cdsl_free_rule(self._ptr)
            self._ptr = None
            self._owned = False

    def __del__(self) -> None:
        self.free()

    def __enter__(self) -> Rule:
        return self

    def __exit__(self, *args) -> None:
        self.free()

    @property
    def ptr(self):
        return self._ptr

    @property
    def name(self) -> Optional[str]:
        if self._ptr:
            n = self._ptr.contents.name
            return n.decode("utf-8") if n else None
        return None

    @property
    def meta(self) -> Dict[str, str]:
        """Return META key-value pairs as a dict."""
        if not self._ptr:
            return {}
        result: Dict[str, str] = {}
        m = self._ptr.contents.meta_list
        while m:
            k = _decode(m.contents.key)
            v = _decode(m.contents.value)
            if k is not None:
                result[k] = v or ""
            m = m.contents.next
        return result

    @property
    def then_action(self) -> Optional[Dict[str, Any]]:
        """Return the THEN action info (name and args)."""
        if not self._ptr or not self._ptr.contents.then_action:
            return None
        a = self._ptr.contents.then_action.contents
        arg_count = 0
        arg = a.args
        while arg:
            arg_count += 1
            arg = arg.contents.next
        return {
            "name": _decode(a.action_name),
            "arg_count": arg_count,
        }

    @property
    def metric_count(self) -> int:
        if not self._ptr:
            return 0
        count = 0
        m = self._ptr.contents.metrics
        while m:
            count += 1
            m = m.contents.next
        return count

    @property
    def when_expr(self) -> Optional[Dict[str, Any]]:
        """Return the WHEN expression as a nested dict tree."""
        if not self._ptr or not self._ptr.contents.when_expr:
            return None
        return _expr_to_dict(self._ptr.contents.when_expr.contents)

    def metrics_info(self) -> List[Dict[str, Any]]:
        """Return info about each METRIC section."""
        if not self._ptr:
            return []
        result = []
        m = self._ptr.contents.metrics
        while m:
            cases = []
            c = m.contents.case_list
            while c:
                cases.append({
                    "condition": (
                        _expr_to_dict(c.contents.condition.contents)
                        if c.contents.condition else None
                    ),
                    "action": _action_to_dict(c.contents.action.contents)
                    if c.contents.action else None,
                })
                c = c.contents.next
            default = None
            if m.contents.default_action:
                default = _action_to_dict(m.contents.default_action.contents)
            meta = {}
            mi = m.contents.meta_list
            while mi:
                k = _decode(mi.contents.key)
                v = _decode(mi.contents.value)
                if k is not None:
                    meta[k] = v or ""
                mi = mi.contents.next
            result.append({
                "name": _decode(m.contents.name),
                "cases": cases,
                "default_action": default,
                "meta": meta,
            })
            m = m.contents.next
        return result

    def __repr__(self) -> str:
        return f"Rule(name={self.name!r}, ptr={self._ptr})"

    def __eq__(self, other) -> bool:
        if not isinstance(other, Rule):
            return NotImplemented
        return self.name is not None and self.name == other.name

    def __hash__(self) -> int:
        n = self.name
        return hash(n) if n else id(self._ptr)


class Context:
    """Execution context holding variable bindings for rule evaluation."""

    def __init__(self, schema: Schema) -> None:
        self._schema = schema
        self._ptr = _lib.cdsl_context_create(schema._ptr)
        if not self._ptr:
            raise MemoryError("cdsl_context_create() returned NULL")

    def free(self) -> None:
        if self._ptr:
            _lib.cdsl_context_free(self._ptr)
            self._ptr = None

    def __del__(self) -> None:
        self.free()

    def __enter__(self) -> Context:
        return self

    def __exit__(self, *args) -> None:
        self.free()

    def __repr__(self) -> str:
        return f"Context(ptr={self._ptr})"

    def set_int(self, name: str, val: int) -> Context:
        _lib.cdsl_context_set_int(self._ptr, name.encode("utf-8"), val)
        return self

    def set_float(self, name: str, val: float) -> Context:
        _lib.cdsl_context_set_float(self._ptr, name.encode("utf-8"), val)
        return self

    def set_bool(self, name: str, val: bool) -> Context:
        _lib.cdsl_context_set_bool(self._ptr, name.encode("utf-8"), int(val))
        return self

    def set_string(self, name: str, val: str) -> Context:
        _lib.cdsl_context_set_string(
            self._ptr, name.encode("utf-8"), val.encode("utf-8"),
        )
        return self

    def set_date(self, name: str, val: int) -> Context:
        _lib.cdsl_context_set_date(self._ptr, name.encode("utf-8"), val)
        return self

    def set_long(self, name: str, val: int) -> Context:
        _lib.cdsl_context_set_long(self._ptr, name.encode("utf-8"), val)
        return self

    def get_int(self, name: str, default: int = 0) -> int:
        return _lib.cdsl_context_get_int(
            self._ptr, name.encode("utf-8"), default,
        )

    def get_float(self, name: str, default: float = 0.0) -> float:
        return _lib.cdsl_context_get_float(
            self._ptr, name.encode("utf-8"), default,
        )

    def get_bool(self, name: str, default: bool = False) -> bool:
        return bool(_lib.cdsl_context_get_bool(
            self._ptr, name.encode("utf-8"), int(default),
        ))

    def get_string(self, name: str, default: str = "") -> Optional[str]:
        result = _lib.cdsl_context_get_string(
            self._ptr, name.encode("utf-8"), default.encode("utf-8"),
        )
        return result.decode("utf-8") if result else default

    def get_date(self, name: str, default: int = 0) -> int:
        return _lib.cdsl_context_get_date(
            self._ptr, name.encode("utf-8"), default,
        )

    def get_long(self, name: str, default: int = 0) -> int:
        return _lib.cdsl_context_get_long(
            self._ptr, name.encode("utf-8"), default,
        )

    def remove(self, name: str) -> Context:
        rc = _lib.cdsl_context_remove(
            self._ptr, name.encode("utf-8"),
        )
        if rc == 0:
            raise DSLError(f"Context.remove('{name}'): variable not found")
        return self

    def load_json(self, json_str: str) -> Context:
        rc = _lib.cdsl_context_load_json(
            self._ptr, json_str.encode("utf-8"),
        )
        if rc != 1:
            raise DSLError("cdsl_context_load_json() failed (invalid JSON?)")
        return self

    def __contains__(self, name: str) -> bool:
        if not self._ptr:
            return False
        e = self._ptr.contents.entries
        name_b = name.encode("utf-8") if isinstance(name, str) else name
        while e:
            if e.contents.name and e.contents.name == name_b:
                return True
            e = e.contents.next
        return False

    def keys(self) -> List[str]:
        if not self._ptr:
            return []
        result: List[str] = []
        e = self._ptr.contents.entries
        while e:
            n = e.contents.name
            if n:
                result.append(n.decode("utf-8"))
            e = e.contents.next
        return result

    def get_type(self, name: str) -> Optional[int]:
        if not self._ptr:
            return None
        e = self._ptr.contents.entries
        name_b = name.encode("utf-8") if isinstance(name, str) else name
        while e:
            if e.contents.name and e.contents.name == name_b:
                return e.contents.value.type
            e = e.contents.next
        return None

    def clear(self) -> Context:
        for k in self.keys():
            try:
                self.remove(k)
            except DSLError:
                pass
        return self


class MetricResult:
    """Result of a single metric evaluation (eagerly copies all data)."""

    def __init__(self, ptr: POINTER(cdsl_metric_result)) -> None:
        c = ptr.contents
        self._metric_name = _decode(c.metric_name) or ""
        self._description = _decode(c.description) or ""
        self._max_weight = c.max_weight
        self._score_obtained = c.score_obtained
        self._is_critical = bool(c.is_critical)
        self._is_passed = bool(c.is_passed)
        self._matched_case_expr = _decode(c.matched_case_expr) or ""
        self._violation_reason = _decode(c.violation_reason) or ""

    @property
    def metric_name(self) -> str:
        return self._metric_name

    @property
    def description(self) -> str:
        return self._description

    @property
    def max_weight(self) -> int:
        return self._max_weight

    @property
    def score_obtained(self) -> int:
        return self._score_obtained

    @property
    def is_critical(self) -> bool:
        return self._is_critical

    @property
    def is_passed(self) -> bool:
        return self._is_passed

    @property
    def matched_case_expr(self) -> str:
        return self._matched_case_expr

    @property
    def violation_reason(self) -> str:
        return self._violation_reason

    def to_dict(self) -> Dict[str, Any]:
        return {
            "metric_name": self._metric_name,
            "description": self._description,
            "max_weight": self._max_weight,
            "score_obtained": self._score_obtained,
            "is_critical": self._is_critical,
            "is_passed": self._is_passed,
            "matched_case_expr": self._matched_case_expr,
            "violation_reason": self._violation_reason,
        }

    def __repr__(self) -> str:
        return (
            f"MetricResult(name={self.metric_name!r}, "
            f"score={self.score_obtained}/{self.max_weight}, "
            f"passed={self.is_passed})"
        )

    def __str__(self) -> str:
        status = "PASS" if self.is_passed else "FAIL"
        return f"[{status}] {self.metric_name} ({self.score_obtained}/{self.max_weight})"


class RuleReport:
    """Result of a single rule execution."""

    def __init__(self, ptr) -> None:
        self._ptr = ptr
        self._owned = False

    @classmethod
    def _from_owned(cls, ptr) -> RuleReport:
        r = cls(ptr)
        r._owned = True
        return r

    def free(self) -> None:
        if self._owned and self._ptr:
            _lib.cdsl_report_free(self._ptr)
            self._ptr = None
            self._owned = False

    def __del__(self) -> None:
        self.free()

    @property
    def rule_name(self) -> Optional[str]:
        return _decode(self._ptr.contents.rule_name)

    @property
    def description(self) -> Optional[str]:
        return _decode(self._ptr.contents.description)

    @property
    def metric_count(self) -> int:
        return self._ptr.contents.metric_count

    @property
    def metrics(self) -> List[MetricResult]:
        count = self._ptr.contents.metric_count
        arr = self._ptr.contents.metrics
        return [MetricResult(ctypes.pointer(arr[i])) for i in range(count)]

    @property
    def total_max_score(self) -> int:
        return self._ptr.contents.total_max_score

    @property
    def total_obtained_score(self) -> int:
        return self._ptr.contents.total_obtained_score

    @property
    def status(self) -> int:
        return self._ptr.contents.status

    @property
    def decision_summary(self) -> Optional[str]:
        return _decode(self._ptr.contents.decision_summary)

    def to_json(self) -> str:
        result = _decode_and_free(_lib.cdsl_report_to_json(self._ptr))
        return result if result else "{}"

    def pprint(self) -> None:
        _lib.cdsl_report_print(self._ptr)

    def __repr__(self) -> str:
        return (
            f"RuleReport(name={self.rule_name!r}, "
            f"status={Status._names.get(self.status, '?')}, "
            f"score={self.total_obtained_score}/{self.total_max_score})"
        )

    def __str__(self) -> str:
        return f"[{Status._names.get(self.status, '?')}] {self.rule_name} ({self.total_obtained_score}/{self.total_max_score})"


class RulesetReport:
    """Result of executing a ruleset."""

    def __init__(self, ptr) -> None:
        self._ptr = ptr
        self._owned = True

    def free(self) -> None:
        if self._owned and self._ptr:
            _lib.cdsl_ruleset_report_free(self._ptr)
            self._ptr = None
            self._owned = False

    def __del__(self) -> None:
        self.free()

    @property
    def rule_count(self) -> int:
        return self._ptr.contents.rule_count

    @property
    def rule_reports(self) -> List[RuleReport]:
        count = self._ptr.contents.rule_count
        arr = self._ptr.contents.rule_reports
        reports = []
        for i in range(count):
            r = RuleReport(arr[i])
            r._owned = False
            reports.append(r)
        return reports

    @property
    def total_passed(self) -> int:
        return self._ptr.contents.total_passed

    @property
    def total_failed(self) -> int:
        return self._ptr.contents.total_failed

    @property
    def aggregate_score(self) -> int:
        return self._ptr.contents.aggregate_score

    @property
    def aggregate_max(self) -> int:
        return self._ptr.contents.aggregate_max

    @property
    def summary(self) -> Optional[str]:
        return _decode(self._ptr.contents.summary)

    def pprint(self) -> None:
        """Pretty-print the report to stdout."""
        _lib.cdsl_ruleset_report_print(self._ptr)

    def __str__(self) -> str:
        errors = self._ptr.contents.total_error if self._ptr else 0
        return (f"PASSED={self.total_passed} FAILED={self.total_failed} "
                f"ERRORS={errors} SCORE={self.aggregate_score}/{self.aggregate_max}")

    def __repr__(self) -> str:
        return (
            f"RulesetReport(passed={self.total_passed}, "
            f"failed={self.total_failed}, "
            f"score={self.aggregate_score}/{self.aggregate_max})"
        )


class VM:
    """CDSL Virtual Machine for rule execution."""

    def __init__(self, schema: Schema) -> None:
        self._schema = schema
        self._ptr = _lib.cdsl_vm_create(schema._ptr)
        if not self._ptr:
            raise MemoryError("cdsl_vm_create() returned NULL")
        self._action_callbacks: List[Any] = []
        self._func_callbacks: List[Any] = []
        self._trace_cb = None

    def free(self) -> None:
        if self._ptr:
            _lib.cdsl_vm_free(self._ptr)
            self._ptr = None

    def __del__(self) -> None:
        self.free()

    def __enter__(self) -> VM:
        return self

    def __exit__(self, *args) -> None:
        self.free()

    def __repr__(self) -> str:
        return f"VM(ptr={self._ptr})"

    def get_max_expr_depth(self) -> int:
        return _lib.cdsl_vm_get_max_expr_depth(self._ptr)

    def set_max_expr_depth(self, depth: int) -> VM:
        _lib.cdsl_vm_set_max_expr_depth(self._ptr, depth)
        return self

    def register_action(
        self,
        name: str,
        cb: Callable[[str, Any, Any], None],
    ) -> None:
        """Register an action callback.

        The callback receives (action_name, args_ptr, user_data).
        args_ptr is a ctypes POINTER(cdsl_arg_node).
        """
        c_cb = cdsl_action_cb_type(cb)
        self._action_callbacks.append(c_cb)
        _lib.cdsl_vm_register_action(
            self._ptr, name.encode("utf-8"), c_cb,
        )

    def register_function(
        self,
        name: str,
        cb: Callable,
    ) -> None:
        """Register a custom function callback."""
        c_cb = cdsl_func_cb_type(cb)
        self._func_callbacks.append(c_cb)
        _lib.cdsl_vm_register_function(
            self._ptr, name.encode("utf-8"), c_cb,
        )

    def set_debug(self, enabled: bool) -> None:
        _lib.cdsl_vm_set_debug(self._ptr, int(enabled))

    def set_timeout(self, timeout_us: int) -> None:
        _lib.cdsl_vm_set_timeout(self._ptr, timeout_us)

    def get_timeout(self) -> int:
        return _lib.cdsl_vm_get_timeout(self._ptr)

    def set_memory_limit(self, limit_bytes: int) -> None:
        _lib.cdsl_vm_set_memory_limit(self._ptr, limit_bytes)

    def get_memory_limit(self) -> int:
        return _lib.cdsl_vm_get_memory_limit(self._ptr)

    def set_instruction_limit(self, limit: int) -> None:
        _lib.cdsl_vm_set_instruction_limit(self._ptr, limit)

    def get_instruction_limit(self) -> int:
        return _lib.cdsl_vm_get_instruction_limit(self._ptr)

    def set_trace_callback(self, cb: Any) -> None:
        self._trace_cb = cdsl_trace_cb_type(cb)
        _lib.cdsl_vm_set_trace_callback(self._ptr, self._trace_cb, None)

    def execute(self, rule: _RuleLike, ctx: Context) -> RuleReport:
        ptr = _lib.cdsl_vm_execute(self._ptr, rule.ptr, ctx._ptr)
        if not ptr:
            raise DSLError("cdsl_vm_execute() returned NULL")
        return RuleReport._from_owned(ptr)

    def execute_compiled(self, compiled: Any, ctx: Context) -> RuleReport:
        ptr = _lib.cdsl_vm_execute_compiled(
            self._ptr, compiled._ptr if hasattr(compiled, '_ptr') else compiled,
            ctx._ptr,
        )
        if not ptr:
            raise DSLError("cdsl_vm_execute_compiled() returned NULL")
        return RuleReport._from_owned(ptr)

    def get_stats(self) -> Dict[str, Any]:
        ptr = _lib.cdsl_vm_get_stats(self._ptr)
        if not ptr:
            return {}
        stats = ptr.contents
        result = {
            "total_executions": stats.total_executions,
            "total_rules_executed": stats.total_rules_executed,
            "total_metrics_evaluated": stats.total_metrics_evaluated,
            "total_actions_triggered": stats.total_actions_triggered,
            "total_time_us": stats.total_time_us,
            "avg_time_us": stats.avg_time_us,
        }
        _lib.free(ctypes.cast(ptr, c_void_p))
        return result

    def reset_stats(self) -> None:
        _lib.cdsl_vm_reset_stats(self._ptr)

    def compile(self, rule: _RuleLike) -> CompiledRule:
        bc = cdsl_bytecode()
        rc = _lib.cdsl_bytecode_compile(
            rule.ptr, self._schema._ptr, byref(bc),
        )
        if rc == 0:
            raise DSLError("Bytecode compilation failed")
        return CompiledRule(bc)

    def execute_bytecode(
        self, compiled: CompiledRule, ctx: Context,
    ) -> RuleReport:
        ptr = _lib.cdsl_bytecode_execute_rule(
            self._ptr, byref(compiled._bc), ctx._ptr,
        )
        if not ptr:
            raise DSLError("cdsl_bytecode_execute_rule() returned NULL")
        return RuleReport._from_owned(ptr)


class Ruleset:
    """Ordered collection of rules."""

    def __init__(self) -> None:
        self._ptr = _lib.cdsl_ruleset_create()
        if not self._ptr:
            raise MemoryError("cdsl_ruleset_create() returned NULL")

    def free(self) -> None:
        if self._ptr:
            _lib.cdsl_ruleset_free(self._ptr)
            self._ptr = None

    def __del__(self) -> None:
        self.free()

    def __enter__(self) -> Ruleset:
        return self

    def __exit__(self, *args) -> None:
        self.free()

    def __repr__(self) -> str:
        return f"Ruleset(count={len(self)})"

    def __len__(self) -> int:
        return self._ptr.contents.count if self._ptr else 0

    def __iter__(self):
        if not self._ptr:
            return
        e = self._ptr.contents.entries
        while e:
            rule_ptr = e.contents.rule
            if rule_ptr:
                yield Rule(rule_ptr, owned=False)
            e = e.contents.next

    def __bool__(self) -> bool:
        return self._ptr is not None and len(self) > 0

    def add(self, rule: _RuleLike, priority: int = 0) -> Ruleset:
        """Add a rule to the ruleset. Ownership transfers to the ruleset."""
        _lib.cdsl_ruleset_add(self._ptr, rule.ptr, priority)
        if isinstance(rule, Rule):
            rule._owned = False
        return self

    def remove(self, rule_name: str) -> Ruleset:
        rc = _lib.cdsl_ruleset_remove(
            self._ptr, rule_name.encode("utf-8"),
        )
        if rc == 0:
            raise DSLError(f"remove('{rule_name}'): rule not found")
        return self

    def load_file(
        self,
        filepath: str,
        priority: int,
        schema: Schema,
    ) -> Ruleset:
        err_buf = ctypes.create_string_buffer(4096)
        rc = _lib.cdsl_ruleset_load_file(
            self._ptr, filepath.encode("utf-8"), priority,
            schema._ptr, err_buf, len(err_buf),
        )
        if rc == 0:
            raise DSLError(err_buf.value.decode("utf-8", errors="replace"))
        return self

    def load_string(
        self,
        dsl_code: str,
        priority: int,
        schema: Schema,
    ) -> Ruleset:
        err_buf = ctypes.create_string_buffer(4096)
        rc = _lib.cdsl_ruleset_load_string(
            self._ptr, dsl_code.encode("utf-8"), priority,
            schema._ptr, err_buf, len(err_buf),
        )
        if rc == 0:
            raise DSLError(err_buf.value.decode("utf-8", errors="replace"))
        return self

    def reload_file(self, filepath: str, schema: Schema) -> Ruleset:
        """Reload rules from a file, replacing existing entries."""
        err_buf = ctypes.create_string_buffer(4096)
        rc = _lib.cdsl_ruleset_reload_file(
            self._ptr, filepath.encode("utf-8"),
            schema._ptr, err_buf, len(err_buf),
        )
        if rc == 0:
            raise DSLError(err_buf.value.decode("utf-8", errors="replace"))
        return self

    def validate_deps(self) -> Ruleset:
        err_buf = ctypes.create_string_buffer(4096)
        rc = _lib.cdsl_ruleset_validate_deps(self._ptr, err_buf, len(err_buf))
        if rc != 0:
            raise DSLError(err_buf.value.decode("utf-8", errors="replace"))
        return self

    def topo_sort(self) -> Ruleset:
        rc = _lib.cdsl_ruleset_topo_sort(self._ptr)
        if rc != 0:
            raise DSLError("cdsl_ruleset_topo_sort() failed")
        return self

    def execute(self, vm: VM, ctx: Context) -> RulesetReport:
        ptr = _lib.cdsl_vm_execute_ruleset(vm._ptr, self._ptr, ctx._ptr)
        if not ptr:
            raise DSLError("cdsl_vm_execute_ruleset() returned NULL")
        return RulesetReport(ptr)

    def execute_parallel(
        self, vm: VM, ctx: Context, threads: int = 4,
    ) -> RulesetReport:
        ptr = _lib.cdsl_vm_execute_ruleset_parallel(
            vm._ptr, self._ptr, ctx._ptr, threads,
        )
        if not ptr:
            raise DSLError("cdsl_vm_execute_ruleset_parallel() returned NULL")
        return RulesetReport(ptr)


class CompileCache:
    """Cache for compiled (bytecode) rules."""

    def __init__(self, capacity: int = 64) -> None:
        self._ptr = _lib.cdsl_compile_cache_create(capacity)
        if not self._ptr:
            raise MemoryError("cdsl_compile_cache_create() returned NULL")

    def free(self) -> None:
        if self._ptr:
            _lib.cdsl_compile_cache_free(self._ptr)
            self._ptr = None

    def __del__(self) -> None:
        self.free()

    def __enter__(self) -> CompileCache:
        return self

    def __exit__(self, *args) -> None:
        self.free()

    def __repr__(self) -> str:
        return f"CompileCache(ptr={self._ptr})"

    def compile(self, dsl_code: str, schema: Schema) -> Any:
        err_buf = ctypes.create_string_buffer(4096)
        ptr = _lib.cdsl_compile(
            self._ptr, dsl_code.encode("utf-8"),
            schema._ptr, err_buf, len(err_buf),
        )
        if not ptr:
            raise DSLError(err_buf.value.decode("utf-8", errors="replace"))
        return ptr

    def remove(self, dsl_code: str) -> CompileCache:
        rc = _lib.cdsl_compile_cache_remove(
            self._ptr, dsl_code.encode("utf-8"),
        )
        if rc == 0:
            raise DSLError("CompileCache.remove(): rule not found")
        return self


class CompiledRule:
    """Bytecode-compiled rule for faster execution."""

    def __init__(self, bc: cdsl_bytecode) -> None:
        self._bc = bc

    def free(self) -> None:
        if self._bc is not None:
            _lib.cdsl_bytecode_free(byref(self._bc))
            self._bc = None

    def __del__(self) -> None:
        self.free()

    def __enter__(self) -> CompiledRule:
        return self

    def __exit__(self, *args) -> None:
        self.free()

    def __repr__(self) -> str:
        count = self._bc.count if self._bc is not None else 0
        return f"CompiledRule(instructions={count})"

    @property
    def instruction_count(self) -> int:
        return self._bc.count if self._bc is not None else 0

    def disassemble(self) -> str:
        if self._bc is None:
            return ""
        lines = []
        inst = self._bc.code
        for i in range(self._bc.count):
            op_name = _BC_OP_NAMES.get(inst[i].op, f"UNKNOWN({inst[i].op})")
            op_val = inst[i].operand
            operand_str = ""
            if inst[i].op <= 6:
                operand_str = str(op_val.int_val)
            elif inst[i].op == 7:
                operand_str = f"#{op_val.const_idx}"
            elif inst[i].op in (21, 26, 28):
                operand_str = f"#{op_val.const_idx}"
            elif 23 <= inst[i].op <= 25:
                operand_str = str(op_val.jump_offset)
            if operand_str:
                lines.append(f"  {i:4d}: {op_name:<15} {operand_str}")
            else:
                lines.append(f"  {i:4d}: {op_name}")
        return "\n".join(lines)

    def execute(self, vm: VM, ctx: Context) -> RuleReport:
        return vm.execute_bytecode(self, ctx)


# ---- Top-level convenience functions -----------------------------------------

def parse(dsl_code: str) -> Rule:
    """Parse a DSL string into a Rule.

    Raises DSLError with detailed messages on parse failure.
    """
    err_ptr = POINTER(cdsl_error_list)()
    ptr = _lib.cdsl_parse_string(
        dsl_code.encode("utf-8"), byref(err_ptr),
    )
    if err_ptr:
        errors = err_ptr.contents
        if errors.count > 0 or not ptr:
            raw = []
            for i in range(errors.count):
                e = errors.errors[i].contents
                raw.append({
                    "line": e.line,
                    "column": e.column,
                    "message": _decode(e.message) or "unknown error",
                    "hint": _decode(e.hint),
                    "kind": e.kind,
                })
            _lib.cdsl_error_list_free(err_ptr)
            if not ptr:
                parts = [f"  line {e['line']}: {e['message']}"
                         + (f" ({e['hint']})" if e['hint'] else "")
                         for e in raw]
                raise DSLError("Parse failed:\n" + "\n".join(parts), errors=raw)
    if not ptr:
        raise DSLError("Parse failed: unknown error")
    return Rule(ptr)


def parse_file(filepath: str) -> Rule:
    """Read a file and parse its content as DSL."""
    with open(filepath, "r") as f:
        return parse(f.read())


def generate_code(rule: _RuleLike, schema: Schema) -> str:
    """Generate C source code for a rule."""
    result = _decode_and_free(
        _lib.cdsl_codegen_rule_to_c(rule.ptr, schema._ptr)
    )
    if not result:
        raise DSLError("Code generation failed")
    return result


def to_dot(rule: _RuleLike) -> str:
    """Generate Graphviz DOT representation of a rule."""
    result = _decode_and_free(_lib.cdsl_rule_to_dot(rule.ptr))
    if not result:
        raise DSLError("DOT generation failed")
    return result


def to_dot_file(rule: _RuleLike, filepath: str) -> None:
    """Write Graphviz DOT representation of a rule to a file."""
    rc = _lib.cdsl_rule_to_dot_file(
        rule.ptr, filepath.encode("utf-8"),
    )
    if rc == 0:
        raise DSLError("to_dot_file() failed")


def ruleset_to_dot(ruleset: Ruleset) -> str:
    """Generate Graphviz DOT for a ruleset."""
    result = _decode_and_free(_lib.cdsl_ruleset_to_dot(ruleset._ptr))
    if not result:
        raise DSLError("DOT generation failed")
    return result


def ruleset_to_dot_file(ruleset: Ruleset, filepath: str) -> None:
    """Write Graphviz DOT for a ruleset to a file."""
    rc = _lib.cdsl_ruleset_to_dot_file(
        ruleset._ptr, filepath.encode("utf-8"),
    )
    if rc == 0:
        raise DSLError("ruleset_to_dot_file() failed")


def codegen_to_file(rule: _RuleLike, schema: Schema, filepath: str) -> None:
    """Generate C source and write to a file."""
    rc = _lib.cdsl_codegen_to_file(
        rule.ptr, schema._ptr, filepath.encode("utf-8"),
    )
    if rc == 0:
        raise DSLError("codegen_to_file() failed")


def codegen_ruleset_to_files(
    ruleset: Ruleset, schema: Schema, basename: str,
) -> None:
    """Generate C source and header files for a ruleset."""
    rc = _lib.cdsl_codegen_ruleset_to_files(
        ruleset._ptr, schema._ptr, basename.encode("utf-8"),
    )
    if rc == 0:
        raise DSLError("codegen_ruleset_to_files() failed")


def codegen_ruleset_to_h(
    ruleset: Ruleset, schema: Schema, guard_name: str,
) -> str:
    """Generate C header code for a ruleset."""
    result = _decode_and_free(
        _lib.cdsl_codegen_ruleset_to_h(
            ruleset._ptr, schema._ptr, guard_name.encode("utf-8"),
        )
    )
    if not result:
        raise DSLError("codegen_ruleset_to_h() failed")
    return result


def codegen_ruleset_to_c(
    ruleset: Ruleset, schema: Schema, header_name: str,
) -> str:
    """Generate C source code for a ruleset (includes #include of header)."""
    result = _decode_and_free(
        _lib.cdsl_codegen_ruleset_to_c(
            ruleset._ptr, schema._ptr, header_name.encode("utf-8"),
        )
    )
    if not result:
        raise DSLError("codegen_ruleset_to_c() failed")
    return result


# ---- Internal helpers ---------------------------------------------------------

def _decode(p) -> Optional[str]:
    """Decode a c_char_p to str, returning None for NULL."""
    if p:
        return p.decode("utf-8")
    return None


def _collect_errors(err_ptr) -> List[Dict[str, Any]]:
    """Convert a cdsl_error_list_t* to Python dicts, then free."""
    if not err_ptr:
        return []
    errors = err_ptr.contents
    result = []
    for i in range(errors.count):
        e = errors.errors[i].contents
        result.append({
            "line": e.line,
            "column": e.column,
            "message": _decode(e.message),
            "hint": _decode(e.hint),
            "kind": e.kind,
        })
    _lib.cdsl_error_list_free(err_ptr)
    return result


def _action_to_dict(action) -> Dict[str, Any]:
    arg_count = 0
    a = action.args
    while a:
        arg_count += 1
        a = a.contents.next
    return {
        "name": _decode(action.action_name),
        "arg_count": arg_count,
    }


def _expr_to_dict(expr) -> Dict[str, Any]:
    """Convert a cdsl_expr_node_t to a Python dict recursively."""
    typ = expr.type
    result: Dict[str, Any] = {"type": typ}
    if typ == ExprType.ID:
        result["value"] = _decode(expr.data.id_val)
    elif typ == ExprType.INT:
        result["value"] = expr.data.int_val
    elif typ == ExprType.FLOAT:
        result["value"] = expr.data.float_val
    elif typ == ExprType.BOOL:
        result["value"] = bool(expr.data.bool_val)
    elif typ == ExprType.STRING:
        result["value"] = _decode(expr.data.string_val)
    elif typ == ExprType.DATE:
        result["value"] = expr.data.date_val
    elif typ == ExprType.LONG:
        result["value"] = expr.data.long_val
    elif typ == ExprType.ARRAY:
        elements = []
        a = expr.data.array.elements
        while a:
            elements.append(_expr_to_dict(a.contents.expr.contents))
            a = a.contents.next
        result["elements"] = elements
    elif typ == ExprType.BINARY:
        result["op"] = expr.data.binary.op
        result["left"] = _expr_to_dict(expr.data.binary.left.contents)
        result["right"] = _expr_to_dict(expr.data.binary.right.contents)
    elif typ == ExprType.UNARY:
        result["op"] = expr.data.unary.op
        result["expr"] = _expr_to_dict(expr.data.unary.expr.contents)
    elif typ == ExprType.CALL:
        args = []
        a = expr.data.call.args
        while a:
            args.append(_expr_to_dict(a.contents.expr.contents))
            a = a.contents.next
        result["func_name"] = _decode(expr.data.call.func_name)
        result["args"] = args
    return result
