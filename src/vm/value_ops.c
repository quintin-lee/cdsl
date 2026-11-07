/**
 * @file src/vm/value_ops.c
 * @brief Unified unary and binary operator evaluation with checked arithmetic.
 */

#include "value_ops.h"
#include <limits.h>
#include <math.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

static int
ck_add_i64(int64_t a, int64_t b, int64_t* out)
{
#if defined(__GNUC__) || defined(__clang__)
	return !__builtin_add_overflow(a, b, out);
#else
	if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) {
		return 0;
	}
	*out = a + b;
	return 1;
#endif
}

static int
ck_sub_i64(int64_t a, int64_t b, int64_t* out)
{
#if defined(__GNUC__) || defined(__clang__)
	return !__builtin_sub_overflow(a, b, out);
#else
	if ((b < 0 && a > INT64_MAX + b) || (b > 0 && a < INT64_MIN + b)) {
		return 0;
	}
	*out = a - b;
	return 1;
#endif
}

static int
ck_mul_i64(int64_t a, int64_t b, int64_t* out)
{
#if defined(__GNUC__) || defined(__clang__)
	return !__builtin_mul_overflow(a, b, out);
#else
	if (a == 0 || b == 0) {
		*out = 0;
		return 1;
	}
	if ((a == -1 && b == INT64_MIN) || (b == -1 && a == INT64_MIN)) {
		return 0;
	}
	if (a > 0) {
		if (b > 0 ? (a > INT64_MAX / b) : (b < INT64_MIN / a)) {
			return 0;
		}
	} else {
		if (b > 0 ? (a < INT64_MIN / b) : (b < INT64_MAX / a)) {
			return 0;
		}
	}
	*out = a * b;
	return 1;
#endif
}

static int
is_numeric_type(cdsl_type_t t)
{
	return t == CDSL_TYPE_INT || t == CDSL_TYPE_LONG || t == CDSL_TYPE_FLOAT;
}

static int
is_zero_divisor(const cdsl_value_t* b)
{
	if (b->type == CDSL_TYPE_INT) {
		return b->data.int_val == 0;
	}
	if (b->type == CDSL_TYPE_LONG) {
		return b->data.long_val == 0;
	}
	if (b->type == CDSL_TYPE_FLOAT) {
		return b->data.float_val == 0.0;
	}
	return 0;
}

static cdsl_valop_status_t
eval_date_offset(time_t base, const cdsl_value_t* delta, int subtract, cdsl_value_t* out)
{
	int64_t sec_delta = 0;
	if (delta->type == CDSL_TYPE_INT || delta->type == CDSL_TYPE_LONG) {
		int64_t days = (delta->type == CDSL_TYPE_LONG) ? delta->data.long_val
							       : (int64_t)delta->data.int_val;
		if (!ck_mul_i64(days, 86400, &sec_delta)) {
			return CDSL_VALOP_OVERFLOW;
		}
	} else if (delta->type == CDSL_TYPE_FLOAT) {
		double raw_sec = delta->data.float_val * 86400.0;
		if (!isfinite(raw_sec) || raw_sec < (double)(INT64_MIN / 2) ||
		    raw_sec > (double)(INT64_MAX / 2)) {
			return CDSL_VALOP_OVERFLOW;
		}
		sec_delta = (int64_t)raw_sec;
	} else {
		return CDSL_VALOP_TYPE_ERROR;
	}

	int64_t base_i64 = (int64_t)base;
	int64_t res_i64 = 0;
	int ok = subtract ? ck_sub_i64(base_i64, sec_delta, &res_i64)
			  : ck_add_i64(base_i64, sec_delta, &res_i64);
	if (!ok) {
		return CDSL_VALOP_OVERFLOW;
	}
	if (sizeof(time_t) == 4 && (res_i64 < INT32_MIN || res_i64 > INT32_MAX)) {
		return CDSL_VALOP_OVERFLOW;
	}
	out->type = CDSL_TYPE_DATE;
	out->data.date_val = (time_t)res_i64;
	return CDSL_VALOP_OK;
}

cdsl_valop_status_t
cdsl_value_binop(cdsl_op_t op, const cdsl_value_t* a, const cdsl_value_t* b, cdsl_value_t* out)
{
	memset(out, 0, sizeof(*out));
	out->type = CDSL_TYPE_VOID;

	if (!a || !b) {
		return CDSL_VALOP_TYPE_ERROR;
	}

	/* Catch explicit division by zero even when LHS evaluated to VOID */
	if (op == CDSL_OP_DIV && is_zero_divisor(b)) {
		return CDSL_VALOP_DIV_ZERO;
	}

	if (a->type == CDSL_TYPE_VOID || b->type == CDSL_TYPE_VOID) {
		if (cdsl_op_is_relational(op)) {
			out->type = CDSL_TYPE_BOOL;
			out->data.bool_val = 0;
		}
		return CDSL_VALOP_VOID_OPERAND;
	}

	/* STRING: equality / inequality only */
	if (a->type == CDSL_TYPE_STRING || b->type == CDSL_TYPE_STRING) {
		if (a->type != CDSL_TYPE_STRING || b->type != CDSL_TYPE_STRING) {
			return CDSL_VALOP_TYPE_ERROR;
		}
		if (op != CDSL_OP_EQ && op != CDSL_OP_NE) {
			return CDSL_VALOP_TYPE_ERROR;
		}
		int cmp = strcmp(a->data.string_val ? a->data.string_val : "",
				 b->data.string_val ? b->data.string_val : "");
		out->type = CDSL_TYPE_BOOL;
		out->data.bool_val = cdsl_op_eval_cmp(op, cmp);
		return CDSL_VALOP_OK;
	}

	/* DATE operations */
	if (a->type == CDSL_TYPE_DATE || b->type == CDSL_TYPE_DATE) {
		if (a->type == CDSL_TYPE_DATE && b->type == CDSL_TYPE_DATE) {
			if (op == CDSL_OP_SUB) {
				double days =
				    difftime(a->data.date_val, b->data.date_val) / 86400.0;
				if (!isfinite(days) || days < (double)INT_MIN ||
				    days > (double)INT_MAX) {
					return CDSL_VALOP_OVERFLOW;
				}
				out->type = CDSL_TYPE_INT;
				out->data.int_val = (int)days;
				return CDSL_VALOP_OK;
			}
			if (cdsl_op_is_relational(op)) {
				time_t t1 = a->data.date_val;
				time_t t2 = b->data.date_val;
				int cmp = (t1 < t2) ? -1 : (t1 > t2) ? 1 : 0;
				out->type = CDSL_TYPE_BOOL;
				out->data.bool_val = cdsl_op_eval_cmp(op, cmp);
				return CDSL_VALOP_OK;
			}
			return CDSL_VALOP_TYPE_ERROR;
		}
		if (a->type == CDSL_TYPE_DATE && (op == CDSL_OP_ADD || op == CDSL_OP_SUB)) {
			return eval_date_offset(a->data.date_val, b, op == CDSL_OP_SUB, out);
		}
		if (b->type == CDSL_TYPE_DATE && op == CDSL_OP_ADD) {
			return eval_date_offset(b->data.date_val, a, 0, out);
		}
		return CDSL_VALOP_TYPE_ERROR;
	}

	/* BOOL: comparison with BOOL only */
	if (a->type == CDSL_TYPE_BOOL || b->type == CDSL_TYPE_BOOL) {
		if (a->type != CDSL_TYPE_BOOL || b->type != CDSL_TYPE_BOOL ||
		    !cdsl_op_is_relational(op)) {
			return CDSL_VALOP_TYPE_ERROR;
		}
		int bv1 = a->data.bool_val ? 1 : 0;
		int bv2 = b->data.bool_val ? 1 : 0;
		out->type = CDSL_TYPE_BOOL;
		out->data.bool_val = cdsl_op_eval_cmp(op, bv1 - bv2);
		return CDSL_VALOP_OK;
	}

	/* Remaining valid types must both be numeric (INT, LONG, FLOAT) */
	if (!is_numeric_type(a->type) || !is_numeric_type(b->type)) {
		return CDSL_VALOP_TYPE_ERROR;
	}

	/* Case 1: At least one FLOAT -> double precision */
	if (a->type == CDSL_TYPE_FLOAT || b->type == CDSL_TYPE_FLOAT) {
		double av = (a->type == CDSL_TYPE_FLOAT)  ? a->data.float_val
			    : (a->type == CDSL_TYPE_LONG) ? (double)a->data.long_val
							  : (double)a->data.int_val;
		double bv = (b->type == CDSL_TYPE_FLOAT)  ? b->data.float_val
			    : (b->type == CDSL_TYPE_LONG) ? (double)b->data.long_val
							  : (double)b->data.int_val;
		if (cdsl_op_is_arithmetic(op)) {
			if (op == CDSL_OP_DIV && bv == 0.0) {
				return CDSL_VALOP_DIV_ZERO;
			}
			out->type = CDSL_TYPE_FLOAT;
			out->data.float_val = (op == CDSL_OP_ADD)   ? (av + bv)
					      : (op == CDSL_OP_SUB) ? (av - bv)
					      : (op == CDSL_OP_MUL) ? (av * bv)
								    : (av / bv);
			return CDSL_VALOP_OK;
		}
		if (cdsl_op_is_relational(op)) {
			int cmp = (fabs(av - bv) < 1e-9) ? 0 : (av < bv) ? -1 : 1;
			out->type = CDSL_TYPE_BOOL;
			out->data.bool_val = cdsl_op_eval_cmp(op, cmp);
			return CDSL_VALOP_OK;
		}
		return CDSL_VALOP_TYPE_ERROR;
	}

	/* Case 2: At least one LONG (and neither FLOAT) -> exact checked int64_t */
	if (a->type == CDSL_TYPE_LONG || b->type == CDSL_TYPE_LONG) {
		int64_t al =
		    (a->type == CDSL_TYPE_LONG) ? a->data.long_val : (int64_t)a->data.int_val;
		int64_t bl =
		    (b->type == CDSL_TYPE_LONG) ? b->data.long_val : (int64_t)b->data.int_val;
		if (cdsl_op_is_arithmetic(op)) {
			int64_t rl = 0;
			if (op == CDSL_OP_ADD) {
				if (!ck_add_i64(al, bl, &rl)) {
					return CDSL_VALOP_OVERFLOW;
				}
			} else if (op == CDSL_OP_SUB) {
				if (!ck_sub_i64(al, bl, &rl)) {
					return CDSL_VALOP_OVERFLOW;
				}
			} else if (op == CDSL_OP_MUL) {
				if (!ck_mul_i64(al, bl, &rl)) {
					return CDSL_VALOP_OVERFLOW;
				}
			} else {
				if (bl == 0) {
					return CDSL_VALOP_DIV_ZERO;
				}
				if (al == INT64_MIN && bl == -1) {
					return CDSL_VALOP_OVERFLOW;
				}
				rl = al / bl;
			}
			out->type = CDSL_TYPE_LONG;
			out->data.long_val = rl;
			return CDSL_VALOP_OK;
		}
		if (cdsl_op_is_relational(op)) {
			int cmp = (al < bl) ? -1 : (al > bl) ? 1 : 0;
			out->type = CDSL_TYPE_BOOL;
			out->data.bool_val = cdsl_op_eval_cmp(op, cmp);
			return CDSL_VALOP_OK;
		}
		return CDSL_VALOP_TYPE_ERROR;
	}

	/* Case 3: Both INT -> exact checked int32 via int64_t intermediate */
	int64_t ai = (int64_t)a->data.int_val;
	int64_t bi = (int64_t)b->data.int_val;
	if (cdsl_op_is_arithmetic(op)) {
		int64_t ri = 0;
		if (op == CDSL_OP_ADD) {
			ri = ai + bi;
		} else if (op == CDSL_OP_SUB) {
			ri = ai - bi;
		} else if (op == CDSL_OP_MUL) {
			ri = ai * bi;
		} else {
			if (bi == 0) {
				return CDSL_VALOP_DIV_ZERO;
			}
			ri = ai / bi;
		}
		if (ri < (int64_t)INT_MIN || ri > (int64_t)INT_MAX) {
			return CDSL_VALOP_OVERFLOW;
		}
		out->type = CDSL_TYPE_INT;
		out->data.int_val = (int)ri;
		return CDSL_VALOP_OK;
	}
	if (cdsl_op_is_relational(op)) {
		int cmp = (ai < bi) ? -1 : (ai > bi) ? 1 : 0;
		out->type = CDSL_TYPE_BOOL;
		out->data.bool_val = cdsl_op_eval_cmp(op, cmp);
		return CDSL_VALOP_OK;
	}

	return CDSL_VALOP_TYPE_ERROR;
}

cdsl_valop_status_t
cdsl_value_unop(cdsl_op_t op, const cdsl_value_t* v, cdsl_value_t* out)
{
	memset(out, 0, sizeof(*out));
	out->type = CDSL_TYPE_VOID;

	if (!v) {
		return CDSL_VALOP_TYPE_ERROR;
	}
	if (v->type == CDSL_TYPE_VOID) {
		if (op == CDSL_OP_NOT) {
			out->type = CDSL_TYPE_BOOL;
			out->data.bool_val = 1;
		}
		return CDSL_VALOP_VOID_OPERAND;
	}

	if (op == CDSL_OP_NOT) {
		if (v->type == CDSL_TYPE_BOOL) {
			out->type = CDSL_TYPE_BOOL;
			out->data.bool_val = !v->data.bool_val;
			return CDSL_VALOP_OK;
		}
		if (v->type == CDSL_TYPE_INT) {
			out->type = CDSL_TYPE_BOOL;
			out->data.bool_val = !v->data.int_val;
			return CDSL_VALOP_OK;
		}
		return CDSL_VALOP_TYPE_ERROR;
	}

	if (op == CDSL_OP_NEG) {
		if (v->type == CDSL_TYPE_INT) {
			if (v->data.int_val == INT_MIN) {
				return CDSL_VALOP_OVERFLOW;
			}
			out->type = CDSL_TYPE_INT;
			out->data.int_val = -v->data.int_val;
			return CDSL_VALOP_OK;
		}
		if (v->type == CDSL_TYPE_LONG) {
			if (v->data.long_val == INT64_MIN) {
				return CDSL_VALOP_OVERFLOW;
			}
			out->type = CDSL_TYPE_LONG;
			out->data.long_val = -v->data.long_val;
			return CDSL_VALOP_OK;
		}
		if (v->type == CDSL_TYPE_FLOAT) {
			out->type = CDSL_TYPE_FLOAT;
			out->data.float_val = -v->data.float_val;
			return CDSL_VALOP_OK;
		}
		return CDSL_VALOP_TYPE_ERROR;
	}

	return CDSL_VALOP_TYPE_ERROR;
}

const char*
cdsl_valop_status_message(cdsl_valop_status_t s)
{
	switch (s) {
	case CDSL_VALOP_DIV_ZERO:
		return "Division by zero";
	case CDSL_VALOP_OVERFLOW:
		return "Integer arithmetic overflow";
	case CDSL_VALOP_TYPE_ERROR:
		return "Incompatible operand types for operator";
	default:
		return "Runtime evaluation error";
	}
}

const char*
cdsl_valop_status_hint(cdsl_valop_status_t s)
{
	switch (s) {
	case CDSL_VALOP_DIV_ZERO:
		return "Guard the divisor with a non-zero check before dividing";
	case CDSL_VALOP_OVERFLOW:
		return "Use LONG (64-bit) values or constrain input ranges";
	case CDSL_VALOP_TYPE_ERROR:
		return "Verify the rule against a schema with cdsl_verify_rule()";
	default:
		return NULL;
	}
}
