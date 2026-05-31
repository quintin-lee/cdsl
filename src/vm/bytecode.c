/**
 * @file src/vm/bytecode.c
 * @brief Bytecode compiler and stack-based VM executor.
 *
 * Compiles DSL rule expressions into a flat instruction stream and executes
 * them using a value-stack architecture with switch-dispatch.
 */

#include "cdsl/bytecode.h"
#include "cdsl/execution.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ---- bytecode chunk management ---- */

static int
bc_ensure_cap(cdsl_bytecode_t* bc)
{
	if (bc->count < bc->capacity) {
		return 1;
	}
	int new_cap = bc->capacity ? bc->capacity * 2 : 64;
	bc_inst_t* new_code = realloc(bc->code, sizeof(bc_inst_t) * new_cap);
	if (!new_code) {
		return 0;
	}
	bc->code = new_code;
	bc->capacity = new_cap;
	return 1;
}

static void
bc_emit(cdsl_bytecode_t* bc, bc_op_t op)
{
	if (!bc_ensure_cap(bc)) {
		return;
	}
	bc_inst_t* inst = &bc->code[bc->count++];
	memset(inst, 0, sizeof(*inst));
	inst->op = op;
}

static void
bc_emit_int(cdsl_bytecode_t* bc, int val)
{
	if (!bc_ensure_cap(bc)) {
		return;
	}
	bc_inst_t* inst = &bc->code[bc->count++];
	memset(inst, 0, sizeof(*inst));
	inst->op = BC_PUSH_INT;
	inst->operand.int_val = val;
}

static void
bc_emit_long(cdsl_bytecode_t* bc, int64_t val)
{
	if (!bc_ensure_cap(bc)) {
		return;
	}
	bc_inst_t* inst = &bc->code[bc->count++];
	memset(inst, 0, sizeof(*inst));
	inst->op = BC_PUSH_LONG;
	inst->operand.long_val = val;
}

static void
bc_emit_float(cdsl_bytecode_t* bc, double val)
{
	if (!bc_ensure_cap(bc)) {
		return;
	}
	bc_inst_t* inst = &bc->code[bc->count++];
	memset(inst, 0, sizeof(*inst));
	inst->op = BC_PUSH_FLOAT;
	inst->operand.float_val = val;
}

static void
bc_emit_bool(cdsl_bytecode_t* bc, int val)
{
	if (!bc_ensure_cap(bc)) {
		return;
	}
	bc_inst_t* inst = &bc->code[bc->count++];
	memset(inst, 0, sizeof(*inst));
	inst->op = BC_PUSH_BOOL;
	inst->operand.bool_val = val;
}

static void
bc_emit_string(cdsl_bytecode_t* bc, const char* val)
{
	if (!bc_ensure_cap(bc)) {
		return;
	}
	bc_inst_t* inst = &bc->code[bc->count++];
	memset(inst, 0, sizeof(*inst));
	inst->op = BC_PUSH_STRING;
	inst->operand.string_val = (char*)val;
}

static void
bc_emit_date(cdsl_bytecode_t* bc, time_t val)
{
	if (!bc_ensure_cap(bc)) {
		return;
	}
	bc_inst_t* inst = &bc->code[bc->count++];
	memset(inst, 0, sizeof(*inst));
	inst->op = BC_PUSH_DATE;
	inst->operand.date_val = val;
}

static void
bc_emit_var(cdsl_bytecode_t* bc, const char* name)
{
	if (!bc_ensure_cap(bc)) {
		return;
	}
	bc_inst_t* inst = &bc->code[bc->count++];
	memset(inst, 0, sizeof(*inst));
	inst->op = BC_PUSH_VAR;
	inst->operand.string_val = (char*)name;
}

static void
bc_emit_call(cdsl_bytecode_t* bc, const char* name, int arg_count)
{
	bc_emit_int(bc, arg_count);
	if (!bc_ensure_cap(bc)) {
		return;
	}
	bc_inst_t* inst = &bc->code[bc->count++];
	memset(inst, 0, sizeof(*inst));
	inst->op = BC_CALL;
	inst->operand.string_val = (char*)name;
}

static int
bc_emit_jmp_placeholder(cdsl_bytecode_t* bc)
{
	if (!bc_ensure_cap(bc)) {
		return -1;
	}
	bc_inst_t* inst = &bc->code[bc->count++];
	memset(inst, 0, sizeof(*inst));
	inst->op = BC_JMP_IF_FALSE;
	inst->operand.jump_offset = -1;
	return bc->count - 1;
}

static int
bc_emit_jmp_true_placeholder(cdsl_bytecode_t* bc)
{
	if (!bc_ensure_cap(bc)) {
		return -1;
	}
	bc_inst_t* inst = &bc->code[bc->count++];
	memset(inst, 0, sizeof(*inst));
	inst->op = BC_JMP_IF_TRUE;
	inst->operand.jump_offset = -1;
	return bc->count - 1;
}

static void
bc_patch_jmp(cdsl_bytecode_t* bc, int inst_idx, int target)
{
	if (inst_idx >= 0 && inst_idx < bc->count) {
		bc->code[inst_idx].operand.jump_offset = target - inst_idx;
	}
}

/* ---- AST → bytecode compiler ---- */

/**
 * @brief Check if an expression is a compile-time constant.
 */
static int
is_const_expr(cdsl_expr_node_t* expr)
{
	switch (expr->type) {
	case CDSL_EXPR_INT:
	case CDSL_EXPR_FLOAT:
	case CDSL_EXPR_BOOL:
	case CDSL_EXPR_STRING:
	case CDSL_EXPR_DATE:
	case CDSL_EXPR_LONG:
		return 1;
	case CDSL_EXPR_UNARY:
		return is_const_expr(expr->data.unary.expr);
	case CDSL_EXPR_BINARY: {
		cdsl_op_t op = expr->data.binary.op;
		if (op == CDSL_OP_AND || op == CDSL_OP_OR) {
			return 0;
		}
		return is_const_expr(expr->data.binary.left) &&
		       is_const_expr(expr->data.binary.right);
	}
	default:
		return 0;
	}
}

static int compile_expr(cdsl_expr_node_t* expr,
			const cdsl_schema_t* schema,
			cdsl_bytecode_t* bc,
			int* stack_used);

/**
 * @brief Evaluate a constant expression at compile time (constant folding).
 */
static int
fold_const_expr(cdsl_expr_node_t* expr, cdsl_value_t* out)
{
	switch (expr->type) {
	case CDSL_EXPR_INT:
		out->type = CDSL_TYPE_INT;
		out->data.int_val = expr->data.int_val;
		return 1;
	case CDSL_EXPR_FLOAT:
		out->type = CDSL_TYPE_FLOAT;
		out->data.float_val = expr->data.float_val;
		return 1;
	case CDSL_EXPR_BOOL:
		out->type = CDSL_TYPE_BOOL;
		out->data.bool_val = expr->data.bool_val;
		return 1;
	case CDSL_EXPR_STRING:
		out->type = CDSL_TYPE_STRING;
		out->data.string_val = expr->data.string_val;
		return 1;
	case CDSL_EXPR_DATE:
		out->type = CDSL_TYPE_DATE;
		out->data.date_val = expr->data.date_val;
		return 1;
	case CDSL_EXPR_LONG:
		out->type = CDSL_TYPE_LONG;
		out->data.long_val = expr->data.long_val;
		return 1;
	case CDSL_EXPR_UNARY: {
		cdsl_value_t v;
		if (!fold_const_expr(expr->data.unary.expr, &v)) {
			return 0;
		}
		if (expr->data.unary.op == CDSL_OP_NOT) {
			out->type = CDSL_TYPE_BOOL;
			out->data.bool_val = !v.data.bool_val;
			return 1;
		}
		if (expr->data.unary.op == CDSL_OP_NEG) {
			if (v.type == CDSL_TYPE_INT) {
				out->type = CDSL_TYPE_INT;
				out->data.int_val = -v.data.int_val;
				return 1;
			}
			if (v.type == CDSL_TYPE_FLOAT) {
				out->type = CDSL_TYPE_FLOAT;
				out->data.float_val = -v.data.float_val;
				return 1;
			}
			if (v.type == CDSL_TYPE_LONG) {
				out->type = CDSL_TYPE_LONG;
				out->data.long_val = -v.data.long_val;
				return 1;
			}
		}
		return 0;
	}
	case CDSL_EXPR_BINARY: {
		cdsl_value_t l, r;
		if (!fold_const_expr(expr->data.binary.left, &l) ||
		    !fold_const_expr(expr->data.binary.right, &r)) {
			return 0;
		}
		cdsl_op_t op = expr->data.binary.op;

		/* DATE arithmetic */
		if (l.type == CDSL_TYPE_DATE && r.type == CDSL_TYPE_DATE) {
			if (op == CDSL_OP_SUB) {
				double diff = difftime(l.data.date_val, r.data.date_val);
				out->type = CDSL_TYPE_INT;
				out->data.int_val = (int)(diff / 86400);
				return 1;
			}
			return 0;
		}
		if (l.type == CDSL_TYPE_DATE &&
		    (r.type == CDSL_TYPE_INT || r.type == CDSL_TYPE_LONG)) {
			int64_t days =
			    (r.type == CDSL_TYPE_LONG) ? r.data.long_val : (int64_t)r.data.int_val;
			out->type = CDSL_TYPE_DATE;
			out->data.date_val = (op == CDSL_OP_ADD)
						 ? l.data.date_val + (time_t)(days * 86400)
						 : l.data.date_val - (time_t)(days * 86400);
			return 1;
		}
		if ((l.type == CDSL_TYPE_INT || l.type == CDSL_TYPE_LONG) &&
		    r.type == CDSL_TYPE_DATE && op == CDSL_OP_ADD) {
			int64_t days =
			    (l.type == CDSL_TYPE_LONG) ? l.data.long_val : (int64_t)l.data.int_val;
			out->type = CDSL_TYPE_DATE;
			out->data.date_val = r.data.date_val + (time_t)(days * 86400);
			return 1;
		}

		/* Numeric arithmetic */
		if (op >= CDSL_OP_ADD && op <= CDSL_OP_DIV) {
			double lv = (l.type == CDSL_TYPE_FLOAT)	 ? l.data.float_val
				    : (l.type == CDSL_TYPE_LONG) ? (double)l.data.long_val
								 : (double)l.data.int_val;
			double rv = (r.type == CDSL_TYPE_FLOAT)	 ? r.data.float_val
				    : (r.type == CDSL_TYPE_LONG) ? (double)r.data.long_val
								 : (double)r.data.int_val;
			if (op == CDSL_OP_DIV && rv == 0.0) {
				return 0;
			}
			double result = (op == CDSL_OP_ADD)   ? lv + rv
					: (op == CDSL_OP_SUB) ? lv - rv
					: (op == CDSL_OP_MUL) ? lv * rv
							      : lv / rv;
			if (l.type == CDSL_TYPE_FLOAT || r.type == CDSL_TYPE_FLOAT) {
				out->type = CDSL_TYPE_FLOAT;
				out->data.float_val = result;
			} else if (l.type == CDSL_TYPE_LONG || r.type == CDSL_TYPE_LONG) {
				out->type = CDSL_TYPE_LONG;
				out->data.long_val = (int64_t)result;
			} else {
				out->type = CDSL_TYPE_INT;
				out->data.int_val = (int)result;
			}
			return 1;
		}

		/* Numeric comparison */
		if (op >= CDSL_OP_EQ && op <= CDSL_OP_GE) {
			double lv = (l.type == CDSL_TYPE_FLOAT)	 ? l.data.float_val
				    : (l.type == CDSL_TYPE_LONG) ? (double)l.data.long_val
								 : (double)l.data.int_val;
			double rv = (r.type == CDSL_TYPE_FLOAT)	 ? r.data.float_val
				    : (r.type == CDSL_TYPE_LONG) ? (double)r.data.long_val
								 : (double)r.data.int_val;
			out->type = CDSL_TYPE_BOOL;
			switch (op) {
			case CDSL_OP_EQ:
				out->data.bool_val = fabs(lv - rv) < 1e-9;
				break;
			case CDSL_OP_NE:
				out->data.bool_val = fabs(lv - rv) >= 1e-9;
				break;
			case CDSL_OP_LT:
				out->data.bool_val = lv < rv;
				break;
			case CDSL_OP_GT:
				out->data.bool_val = lv > rv;
				break;
			case CDSL_OP_LE:
				out->data.bool_val = lv <= rv;
				break;
			case CDSL_OP_GE:
				out->data.bool_val = lv >= rv;
				break;
			default:
				return 0;
			}
			return 1;
		}
		return 0;
	}
	default:
		return 0;
	}
}

static int
compile_expr(cdsl_expr_node_t* expr,
	     const cdsl_schema_t* schema,
	     cdsl_bytecode_t* bc,
	     int* stack_used)
{
	if (!expr) {
		return 0;
	}

	int max_stack = 0;

	/* Constant folding: if entire expression is constant, fold it */
	if (is_const_expr(expr)) {
		cdsl_value_t folded;
		if (fold_const_expr(expr, &folded)) {
			switch (folded.type) {
			case CDSL_TYPE_INT:
				bc_emit_int(bc, folded.data.int_val);
				break;
			case CDSL_TYPE_FLOAT:
				bc_emit_float(bc, folded.data.float_val);
				break;
			case CDSL_TYPE_BOOL:
				bc_emit_bool(bc, folded.data.bool_val);
				break;
			case CDSL_TYPE_STRING:
				bc_emit_string(bc, folded.data.string_val);
				break;
			case CDSL_TYPE_DATE:
				bc_emit_date(bc, folded.data.date_val);
				break;
			case CDSL_TYPE_LONG:
				bc_emit_long(bc, folded.data.long_val);
				break;
			default:
				break;
			}
			*stack_used = max_stack + 1;
			bc->max_stack =
			    (max_stack + 1 > bc->max_stack) ? max_stack + 1 : bc->max_stack;
			return 1;
		}
	}

	switch (expr->type) {
	case CDSL_EXPR_INT:
		bc_emit_int(bc, expr->data.int_val);
		max_stack = 1;
		break;
	case CDSL_EXPR_FLOAT:
		bc_emit_float(bc, expr->data.float_val);
		max_stack = 1;
		break;
	case CDSL_EXPR_BOOL:
		bc_emit_bool(bc, expr->data.bool_val);
		max_stack = 1;
		break;
	case CDSL_EXPR_STRING:
		bc_emit_string(bc, expr->data.string_val);
		max_stack = 1;
		break;
	case CDSL_EXPR_DATE:
		bc_emit_date(bc, expr->data.date_val);
		max_stack = 1;
		break;
	case CDSL_EXPR_LONG:
		bc_emit_long(bc, expr->data.long_val);
		max_stack = 1;
		break;
	case CDSL_EXPR_ID:
		bc_emit_var(bc, expr->data.id_val);
		max_stack = 1;
		break;

	case CDSL_EXPR_UNARY: {
		int sub_stack = 0;
		if (!compile_expr(expr->data.unary.expr, schema, bc, &sub_stack)) {
			return 0;
		}
		if (expr->data.unary.op == CDSL_OP_NOT) {
			bc_emit(bc, BC_NOT);
		} else {
			bc_emit(bc, BC_NEG);
		}
		max_stack = sub_stack;
		break;
	}

	case CDSL_EXPR_BINARY: {
		cdsl_op_t op = expr->data.binary.op;

		/* Short-circuit AND: left is false → skip right, leave false on stack */
		if (op == CDSL_OP_AND) {
			int left_stack = 0;
			if (!compile_expr(expr->data.binary.left, schema, bc, &left_stack)) {
				return 0;
			}
			int jmp_idx = bc_emit_jmp_placeholder(bc);
			bc_emit(bc, BC_POP);
			int right_stack = 0;
			if (!compile_expr(expr->data.binary.right, schema, bc, &right_stack)) {
				return 0;
			}
			bc_patch_jmp(bc, jmp_idx, bc->count);
			max_stack = left_stack > right_stack + 1 ? left_stack : right_stack + 1;
			break;
		}

		/* Short-circuit OR: left is true → skip right, leave true on stack */
		if (op == CDSL_OP_OR) {
			int left_stack = 0;
			if (!compile_expr(expr->data.binary.left, schema, bc, &left_stack)) {
				return 0;
			}
			int jmp_idx = bc_emit_jmp_true_placeholder(bc);
			bc_emit(bc, BC_POP);
			int right_stack = 0;
			if (!compile_expr(expr->data.binary.right, schema, bc, &right_stack)) {
				return 0;
			}
			bc_patch_jmp(bc, jmp_idx, bc->count);
			max_stack = left_stack > right_stack + 1 ? left_stack : right_stack + 1;
			break;
		}

		/* Regular binary: compile left, compile right, apply operator */
		int l_stack = 0, r_stack = 0;
		if (!compile_expr(expr->data.binary.left, schema, bc, &l_stack)) {
			return 0;
		}
		if (!compile_expr(expr->data.binary.right, schema, bc, &r_stack)) {
			return 0;
		}
		switch (op) {
		case CDSL_OP_ADD:
			bc_emit(bc, BC_ADD);
			break;
		case CDSL_OP_SUB:
			bc_emit(bc, BC_SUB);
			break;
		case CDSL_OP_MUL:
			bc_emit(bc, BC_MUL);
			break;
		case CDSL_OP_DIV:
			bc_emit(bc, BC_DIV);
			break;
		case CDSL_OP_EQ:
			bc_emit(bc, BC_EQ);
			break;
		case CDSL_OP_NE:
			bc_emit(bc, BC_NE);
			break;
		case CDSL_OP_LT:
			bc_emit(bc, BC_LT);
			break;
		case CDSL_OP_GT:
			bc_emit(bc, BC_GT);
			break;
		case CDSL_OP_LE:
			bc_emit(bc, BC_LE);
			break;
		case CDSL_OP_GE:
			bc_emit(bc, BC_GE);
			break;
		default:
			break;
		}
		max_stack = l_stack > r_stack + 1 ? l_stack : r_stack + 1;
		break;
	}

	case CDSL_EXPR_CALL: {
		int arg_stack = 0;
		cdsl_arg_node_t* arg = expr->data.call.args;
		int arg_count = 0;
		while (arg) {
			int sub = 0;
			compile_expr(arg->expr, schema, bc, &sub);
			if (sub > arg_stack) {
				arg_stack = sub;
			}
			arg = arg->next;
			arg_count++;
		}
		arg_stack = arg_stack > arg_count + 1 ? arg_stack : arg_count + 1;
		bc_emit_call(bc, expr->data.call.func_name, arg_count);
		max_stack = arg_stack;
		break;
	}

	default:
		return 0;
	}

	*stack_used = max_stack;
	if (max_stack > bc->max_stack) {
		bc->max_stack = max_stack;
	}
	return 1;
}

/* ---- public API ---- */

int
cdsl_bytecode_compile(const cdsl_rule_t* rule, const cdsl_schema_t* schema, cdsl_bytecode_t* bc)
{
	if (!rule || !bc) {
		return 0;
	}
	bc->code = NULL;
	bc->count = 0;
	bc->capacity = 0;
	bc->max_stack = 0;

	int stack_used = 0;
	int compiled = 0;
	if (rule->metrics) {
		for (cdsl_metric_node_t* m = rule->metrics; m; m = m->next) {
			for (cdsl_case_node_t* c = m->case_list; c; c = c->next) {
				if (c->condition) {
					int sub = 0;
					compile_expr(c->condition, schema, bc, &sub);
					bc_emit(bc, BC_POP);
					compiled = 1;
				}
			}
		}
		stack_used = 1;
	} else if (rule->when_expr) {
		int sub = 0;
		compile_expr(rule->when_expr, schema, bc, &sub);
		stack_used = sub;
		compiled = 1;
	}

	bc_emit(bc, BC_RET);
	if (bc->max_stack < stack_used) {
		bc->max_stack = stack_used;
	}

	return compiled;
}

/* ---- bytecode VM executor ---- */

cdsl_value_t
cdsl_bytecode_execute(cdsl_vm_t* vm, const cdsl_bytecode_t* bc, cdsl_context_t* ctx)
{
	cdsl_value_t result = {.type = CDSL_TYPE_VOID, .data = {.int_val = 0}};
	if (!vm || !bc || !bc->code) {
		return result;
	}

	double t0 = cdsl_get_time_us_internal();
	cdsl_value_t stack[CDSL_BYTECODE_MAX_STACK];
	int sp = 0;

#define PUSH(v)                                                                                    \
	do {                                                                                       \
		if (sp < CDSL_BYTECODE_MAX_STACK) {                                                \
			stack[sp++] = (v);                                                         \
		}                                                                                  \
	} while (0)
#define POP() (sp > 0 ? stack[--sp] : result)
#define PEEK() (sp > 0 ? stack[sp - 1] : result)

	const bc_inst_t* ip = bc->code;
	const bc_inst_t* end = bc->code + bc->count;
	int loop_cnt = 0;

	while (ip < end) {
		/* Periodic timeout/OOM check */
		if ((++loop_cnt & 0x3FF) == 0 && cdsl_vm_check_abort(vm, t0)) {
			result.type = CDSL_TYPE_VOID;
			goto done;
		}
		switch (ip->op) {
		case BC_PUSH_INT:
			result.type = CDSL_TYPE_INT;
			result.data.int_val = ip->operand.int_val;
			PUSH(result);
			ip++;
			break;
		case BC_PUSH_FLOAT:
			result.type = CDSL_TYPE_FLOAT;
			result.data.float_val = ip->operand.float_val;
			PUSH(result);
			ip++;
			break;
		case BC_PUSH_BOOL:
			result.type = CDSL_TYPE_BOOL;
			result.data.bool_val = ip->operand.bool_val;
			PUSH(result);
			ip++;
			break;
		case BC_PUSH_STRING:
			result.type = CDSL_TYPE_STRING;
			result.data.string_val = ip->operand.string_val;
			PUSH(result);
			ip++;
			break;
		case BC_PUSH_DATE:
			result.type = CDSL_TYPE_DATE;
			result.data.date_val = ip->operand.date_val;
			PUSH(result);
			ip++;
			break;
		case BC_PUSH_LONG:
			result.type = CDSL_TYPE_LONG;
			result.data.long_val = ip->operand.long_val;
			PUSH(result);
			ip++;
			break;
		case BC_PUSH_VAR: {
			cdsl_context_entry_t* e =
			    cdsl_context_get_entry_internal(ctx, ip->operand.string_val);
			if (e) {
				PUSH(e->value);
			} else {
				result.type = CDSL_TYPE_VOID;
				result.data.int_val = 0;
				PUSH(result);
			}
			ip++;
			break;
		}
		case BC_POP:
			if (sp > 0) {
				sp--;
			}
			ip++;
			break;

		/* Arithmetic */
		case BC_ADD:
		case BC_SUB:
		case BC_MUL:
		case BC_DIV: {
			cdsl_value_t b = POP();
			cdsl_value_t a = POP();
			int has_flt = (a.type == CDSL_TYPE_FLOAT || b.type == CDSL_TYPE_FLOAT);
			int has_lng =
			    (!has_flt && (a.type == CDSL_TYPE_LONG || b.type == CDSL_TYPE_LONG));
			double av = (a.type == CDSL_TYPE_FLOAT)	 ? a.data.float_val
				    : (a.type == CDSL_TYPE_LONG) ? (double)a.data.long_val
								 : (double)a.data.int_val;
			double bv = (b.type == CDSL_TYPE_FLOAT)	 ? b.data.float_val
				    : (b.type == CDSL_TYPE_LONG) ? (double)b.data.long_val
								 : (double)b.data.int_val;
			if (ip->op == BC_DIV && bv == 0.0) {
				result.type = CDSL_TYPE_VOID;
			} else {
				double r = (ip->op == BC_ADD)	? av + bv
					   : (ip->op == BC_SUB) ? av - bv
					   : (ip->op == BC_MUL) ? av * bv
								: av / bv;
				if (has_flt) {
					result.type = CDSL_TYPE_FLOAT;
					result.data.float_val = r;
				} else if (has_lng) {
					result.type = CDSL_TYPE_LONG;
					result.data.long_val = (int64_t)r;
				} else {
					result.type = CDSL_TYPE_INT;
					result.data.int_val = (int)r;
				}
			}
			PUSH(result);
			ip++;
			break;
		}

		/* Comparison */
		case BC_EQ:
		case BC_NE:
		case BC_LT:
		case BC_GT:
		case BC_LE:
		case BC_GE: {
			cdsl_value_t b = POP();
			cdsl_value_t a = POP();
			result.type = CDSL_TYPE_BOOL;

			/* String comparison */
			if (a.type == CDSL_TYPE_STRING && b.type == CDSL_TYPE_STRING) {
				int c = strcmp(a.data.string_val ? a.data.string_val : "",
					       b.data.string_val ? b.data.string_val : "");
				result.data.bool_val = (ip->op == BC_EQ)   ? (c == 0)
						       : (ip->op == BC_NE) ? (c != 0)
						       : (ip->op == BC_LT) ? (c < 0)
						       : (ip->op == BC_GT) ? (c > 0)
						       : (ip->op == BC_LE) ? (c <= 0)
									   : (c >= 0);
				PUSH(result);
				ip++;
				break;
			}

			/* Date comparison */
			if (a.type == CDSL_TYPE_DATE && b.type == CDSL_TYPE_DATE) {
				time_t ta = a.data.date_val, tb = b.data.date_val;
				result.data.bool_val = (ip->op == BC_EQ)   ? (ta == tb)
						       : (ip->op == BC_NE) ? (ta != tb)
						       : (ip->op == BC_LT) ? (ta < tb)
						       : (ip->op == BC_GT) ? (ta > tb)
						       : (ip->op == BC_LE) ? (ta <= tb)
									   : (ta >= tb);
				PUSH(result);
				ip++;
				break;
			}

			/* Numeric comparison */
			double av = (a.type == CDSL_TYPE_FLOAT)	 ? a.data.float_val
				    : (a.type == CDSL_TYPE_LONG) ? (double)a.data.long_val
								 : (double)a.data.int_val;
			double bv = (b.type == CDSL_TYPE_FLOAT)	 ? b.data.float_val
				    : (b.type == CDSL_TYPE_LONG) ? (double)b.data.long_val
								 : (double)b.data.int_val;
			result.data.bool_val = (ip->op == BC_EQ)   ? fabs(av - bv) < 1e-9
					       : (ip->op == BC_NE) ? fabs(av - bv) >= 1e-9
					       : (ip->op == BC_LT) ? av < bv
					       : (ip->op == BC_GT) ? av > bv
					       : (ip->op == BC_LE) ? av <= bv
								   : av >= bv;
			PUSH(result);
			ip++;
			break;
		}

		case BC_NOT: {
			cdsl_value_t v = POP();
			result.type = CDSL_TYPE_BOOL;
			result.data.bool_val = !v.data.bool_val;
			PUSH(result);
			ip++;
			break;
		}

		case BC_NEG: {
			cdsl_value_t v = POP();
			if (v.type == CDSL_TYPE_INT) {
				result.type = CDSL_TYPE_INT;
				result.data.int_val = -v.data.int_val;
			} else if (v.type == CDSL_TYPE_FLOAT) {
				result.type = CDSL_TYPE_FLOAT;
				result.data.float_val = -v.data.float_val;
			} else if (v.type == CDSL_TYPE_LONG) {
				result.type = CDSL_TYPE_LONG;
				result.data.long_val = -v.data.long_val;
			} else {
				result.type = CDSL_TYPE_VOID;
			}
			PUSH(result);
			ip++;
			break;
		}

		case BC_CALL: {
			const char* fn_name = ip->operand.string_val;
			cdsl_value_t nv = POP();
			int nargs = nv.data.int_val;
			/* Build arg list from stack (reverse since stack is LIFO) */
			cdsl_arg_node_t* arg_list = NULL;
			cdsl_arg_node_t* arg_nodes =
			    calloc(nargs > 0 ? nargs : 1, sizeof(cdsl_arg_node_t));
			if (!arg_nodes) {
				result.type = CDSL_TYPE_VOID;
				PUSH(result);
				ip++;
				break;
			}
			for (int i = nargs - 1; i >= 0; i--) {
				cdsl_value_t av = POP();
				arg_nodes[i].expr = calloc(1, sizeof(cdsl_expr_node_t));
				if (arg_nodes[i].expr) {
					*arg_nodes[i].expr = (cdsl_expr_node_t){
					    .type = CDSL_EXPR_INT, .data = {.int_val = 0}};
					/* Map runtime value type to expression node type */
					arg_nodes[i].expr->type =
					    (av.type == CDSL_TYPE_INT)	    ? CDSL_EXPR_INT
					    : (av.type == CDSL_TYPE_FLOAT)  ? CDSL_EXPR_FLOAT
					    : (av.type == CDSL_TYPE_BOOL)   ? CDSL_EXPR_BOOL
					    : (av.type == CDSL_TYPE_STRING) ? CDSL_EXPR_STRING
					    : (av.type == CDSL_TYPE_DATE)   ? CDSL_EXPR_DATE
					    : (av.type == CDSL_TYPE_LONG)   ? CDSL_EXPR_LONG
									    : CDSL_EXPR_INT;
					memcpy(&arg_nodes[i].expr->data, &av.data, sizeof(av.data));
				}
			}
			for (int i = 0; i < nargs; i++) {
				arg_nodes[i].next = (i + 1 < nargs) ? &arg_nodes[i + 1] : NULL;
			}
			arg_list = (nargs > 0) ? &arg_nodes[0] : NULL;

			result.type = CDSL_TYPE_VOID;
			for (cdsl_func_entry_t* fn = vm->functions; fn; fn = fn->next) {
				if (strcmp(fn->func_name, fn_name) == 0) {
					result = fn->cb(fn_name, arg_list, ctx, vm);
					break;
				}
			}

			for (int i = 0; i < nargs; i++) {
				free(arg_nodes[i].expr);
			}
			free(arg_nodes);
			PUSH(result);
			ip++;
			break;
		}

		case BC_DUP:
			if (sp > 0) {
				cdsl_value_t top = stack[sp - 1];
				if (sp < CDSL_BYTECODE_MAX_STACK) {
					stack[sp++] = top;
				}
			}
			ip++;
			break;

		case BC_JMP_IF_TRUE: {
			cdsl_value_t v = PEEK();
			if (v.data.bool_val) {
				ip += ip->operand.jump_offset;
			} else {
				POP();
				ip++;
			}
			break;
		}

		case BC_JMP_IF_FALSE: {
			cdsl_value_t v = PEEK();
			if (!v.data.bool_val) {
				ip += ip->operand.jump_offset;
			} else {
				POP();
				ip++;
			}
			break;
		}

		case BC_JMP:
			ip += ip->operand.jump_offset;
			break;

		case BC_RET:
			result = POP();
			goto done;
		}
	}

done:
#undef PUSH
#undef POP
#undef PEEK
	return result;
}

void
cdsl_bytecode_free(cdsl_bytecode_t* bc)
{
	if (!bc) {
		return;
	}
	free(bc->code);
	bc->code = NULL;
	bc->count = 0;
	bc->capacity = 0;
	bc->max_stack = 0;
}
