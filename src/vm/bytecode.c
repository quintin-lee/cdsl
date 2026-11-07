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
#include "value_ops.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>

/* ---- opcode mapping ---- */

bc_op_t
cdsl_op_to_bc(cdsl_op_t op)
{
	switch (op) {
	case CDSL_OP_ADD:
		return BC_ADD;
	case CDSL_OP_SUB:
		return BC_SUB;
	case CDSL_OP_MUL:
		return BC_MUL;
	case CDSL_OP_DIV:
		return BC_DIV;
	case CDSL_OP_EQ:
		return BC_EQ;
	case CDSL_OP_NE:
		return BC_NE;
	case CDSL_OP_LT:
		return BC_LT;
	case CDSL_OP_GT:
		return BC_GT;
	case CDSL_OP_LE:
		return BC_LE;
	case CDSL_OP_GE:
		return BC_GE;
	case CDSL_OP_NOT:
		return BC_NOT;
	case CDSL_OP_NEG:
		return BC_NEG;
	case CDSL_OP_IN:
		return BC_IN;
	default:
		return (bc_op_t)-1;
	}
}

cdsl_op_t
cdsl_bc_to_op(bc_op_t bc)
{
	switch (bc) {
	case BC_ADD:
		return CDSL_OP_ADD;
	case BC_SUB:
		return CDSL_OP_SUB;
	case BC_MUL:
		return CDSL_OP_MUL;
	case BC_DIV:
		return CDSL_OP_DIV;
	case BC_EQ:
		return CDSL_OP_EQ;
	case BC_NE:
		return CDSL_OP_NE;
	case BC_LT:
		return CDSL_OP_LT;
	case BC_GT:
		return CDSL_OP_GT;
	case BC_LE:
		return CDSL_OP_LE;
	case BC_GE:
		return CDSL_OP_GE;
	case BC_NOT:
		return CDSL_OP_NOT;
	case BC_NEG:
		return CDSL_OP_NEG;
	case BC_IN:
		return CDSL_OP_IN;
	default:
		return (cdsl_op_t)-1;
	}
}

/* ---- bytecode chunk management ---- */

static int
bc_add_constant(cdsl_bytecode_t* bc, cdsl_value_t val)
{
	/* Deduplication for strings */
	if (val.type == CDSL_TYPE_STRING) {
		for (int i = 0; i < bc->const_count; i++) {
			if (bc->constants[i].type == CDSL_TYPE_STRING &&
			    strcmp(bc->constants[i].data.string_val, val.data.string_val) == 0) {
				return i;
			}
		}
	}

	if (bc->const_count >= bc->const_capacity) {
		int new_cap = bc->const_capacity ? bc->const_capacity * 2 : 16;
		cdsl_value_t* new_consts = realloc(bc->constants, sizeof(cdsl_value_t) * new_cap);
		if (!new_consts) {
			return -1;
		}
		bc->constants = new_consts;
		bc->const_capacity = new_cap;
	}
	int idx = bc->const_count++;
	// NOLINTNEXTLINE(clang-analyzer-security.ArrayBound)
	bc->constants[idx] = val;
	if (val.type == CDSL_TYPE_STRING) {
		char* s = cdsl_strdup(val.data.string_val);
		if (!s && val.data.string_val) {
			bc->const_count--;
			return -1;
		}
		bc->constants[idx].data.string_val = s;
	}
	return idx;
}

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
	cdsl_value_t v = {.type = CDSL_TYPE_STRING, .data = {.string_val = (char*)val}};
	int idx = bc_add_constant(bc, v);
	bc_inst_t* inst = &bc->code[bc->count++];
	memset(inst, 0, sizeof(*inst));
	inst->op = BC_PUSH_STRING;
	inst->operand.const_idx = idx;
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
	cdsl_value_t v = {.type = CDSL_TYPE_STRING, .data = {.string_val = (char*)name}};
	int idx = bc_add_constant(bc, v);
	bc_inst_t* inst = &bc->code[bc->count++];
	memset(inst, 0, sizeof(*inst));
	inst->op = BC_PUSH_VAR;
	inst->operand.const_idx = idx;
}

static void
bc_emit_call(cdsl_bytecode_t* bc, const char* name, int arg_count)
{
	bc_emit_int(bc, arg_count);
	if (!bc_ensure_cap(bc)) {
		return;
	}
	cdsl_value_t v = {.type = CDSL_TYPE_STRING, .data = {.string_val = (char*)name}};
	int idx = bc_add_constant(bc, v);
	bc_inst_t* inst = &bc->code[bc->count++];
	memset(inst, 0, sizeof(*inst));
	inst->op = BC_CALL;
	inst->operand.const_idx = idx;
}

static void
bc_emit_metric_start(cdsl_bytecode_t* bc, const char* name)
{
	if (!bc_ensure_cap(bc)) {
		return;
	}
	cdsl_value_t v = {.type = CDSL_TYPE_STRING, .data = {.string_val = (char*)name}};
	int idx = bc_add_constant(bc, v);
	bc_inst_t* inst = &bc->code[bc->count++];
	memset(inst, 0, sizeof(*inst));
	inst->op = BC_METRIC_START;
	inst->operand.const_idx = idx;
}

static void
bc_emit_fail_metric(cdsl_bytecode_t* bc, const char* reason)
{
	if (!bc_ensure_cap(bc)) {
		return;
	}
	cdsl_value_t v = {.type = CDSL_TYPE_STRING, .data = {.string_val = (char*)reason}};
	int idx = bc_add_constant(bc, v);
	bc_inst_t* inst = &bc->code[bc->count++];
	memset(inst, 0, sizeof(*inst));
	inst->op = BC_FAIL_METRIC;
	inst->operand.const_idx = idx;
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

static int
bc_emit_jmp_uncond_placeholder(cdsl_bytecode_t* bc)
{
	if (!bc_ensure_cap(bc)) {
		return -1;
	}
	bc_inst_t* inst = &bc->code[bc->count++];
	memset(inst, 0, sizeof(*inst));
	inst->op = BC_JMP;
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
		if (cdsl_op_is_logical_binary(op)) {
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
	if (!expr || !out) {
		return 0;
	}
	memset(out, 0, sizeof(*out));
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
		cdsl_valop_status_t st = cdsl_value_unop(expr->data.unary.op, &v, out);
		cdsl_value_free(&v);
		return (st == CDSL_VALOP_OK);
	}
	case CDSL_EXPR_BINARY: {
		cdsl_value_t l, r;
		if (!fold_const_expr(expr->data.binary.left, &l) ||
		    !fold_const_expr(expr->data.binary.right, &r)) {
			return 0;
		}
		cdsl_op_t op = expr->data.binary.op;
		if (op == CDSL_OP_AND || op == CDSL_OP_OR) {
			cdsl_value_free(&l);
			cdsl_value_free(&r);
			return 0;
		}
		cdsl_valop_status_t st = cdsl_value_binop(op, &l, &r, out);
		cdsl_value_free(&l);
		cdsl_value_free(&r);
		return (st == CDSL_VALOP_OK);
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
	case CDSL_EXPR_ARRAY: {
		int arg_stack = 0;
		cdsl_arg_node_t* arg = expr->data.array.elements;
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
		if (!bc_ensure_cap(bc)) {
			return 0;
		}
		bc_inst_t* inst = &bc->code[bc->count++];
		memset(inst, 0, sizeof(*inst));
		inst->op = BC_PUSH_ARRAY;
		inst->operand.int_val = arg_count;
		max_stack = arg_stack + 1;
		break;
	}
	case CDSL_EXPR_ID:
		bc_emit_var(bc, expr->data.id_val);
		max_stack = 1;
		break;

	case CDSL_EXPR_UNARY: {
		int sub_stack = 0;
		if (!compile_expr(expr->data.unary.expr, schema, bc, &sub_stack)) {
			return 0;
		}
		bc_op_t bcop = cdsl_op_to_bc(expr->data.unary.op);
		if ((int)bcop != -1) {
			bc_emit(bc, bcop);
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

		/* Range inclusion check: left IN [min..max] */
		if (op == CDSL_OP_IN && expr->data.binary.right->type == CDSL_EXPR_RANGE) {
			int l_stack = 0, min_stack = 0, max_stack_val = 0;
			if (!compile_expr(expr->data.binary.left, schema, bc, &l_stack)) {
				return 0;
			}
			cdsl_expr_node_t* rng = expr->data.binary.right;
			if (!compile_expr(rng->data.range.min_expr, schema, bc, &min_stack)) {
				return 0;
			}
			if (!compile_expr(rng->data.range.max_expr, schema, bc, &max_stack_val)) {
				return 0;
			}
			if (!bc_ensure_cap(bc)) {
				return 0;
			}
			bc_inst_t* inst = &bc->code[bc->count++];
			memset(inst, 0, sizeof(*inst));
			inst->op = BC_IN_RANGE;
			max_stack = l_stack > min_stack + 1 ? l_stack : min_stack + 1;
			if (max_stack_val + 2 > max_stack) {
				max_stack = max_stack_val + 2;
			}
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
		bc_op_t bcop = cdsl_op_to_bc(op);
		if ((int)bcop != -1) {
			bc_emit(bc, bcop);
		}
		max_stack = l_stack > r_stack + 1 ? l_stack : r_stack + 1;
		break;
	}

	case CDSL_EXPR_MAP: {
		int arg_stack = 0;
		int count = 0;
		for (cdsl_map_ast_entry_t* me = expr->data.map.entries; me; me = me->next) {
			bc_emit_string(bc, me->key);
			int val_stack = 0;
			if (!compile_expr(me->value, schema, bc, &val_stack)) {
				return 0;
			}
			if (val_stack + 1 > arg_stack) {
				arg_stack = val_stack + 1;
			}
			count++;
		}
		if (!bc_ensure_cap(bc)) {
			return 0;
		}
		bc_inst_t* inst = &bc->code[bc->count++];
		memset(inst, 0, sizeof(*inst));
		inst->op = BC_BUILD_MAP;
		inst->operand.int_val = count;
		max_stack = arg_stack + count * 2;
		break;
	}

	case CDSL_EXPR_SUBSCRIPT: {
		int target_stack = 0, index_stack = 0;
		if (!compile_expr(expr->data.subscript.target, schema, bc, &target_stack)) {
			return 0;
		}
		if (!compile_expr(expr->data.subscript.index, schema, bc, &index_stack)) {
			return 0;
		}
		if (!bc_ensure_cap(bc)) {
			return 0;
		}
		bc_inst_t* inst = &bc->code[bc->count++];
		memset(inst, 0, sizeof(*inst));
		inst->op = BC_GET_INDEX;
		max_stack = target_stack > index_stack + 1 ? target_stack : index_stack + 1;
		break;
	}

	case CDSL_EXPR_RANGE:
	case CDSL_EXPR_LAMBDA:
	case CDSL_EXPR_ERROR:
		return 0;

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

static int
compile_action_call(cdsl_action_node_t* action,
		    const cdsl_schema_t* schema,
		    cdsl_bytecode_t* bc,
		    int* stack_used)
{
	if (!action || !action->action_name) {
		return 1;
	}
	int arg_stack = 0;
	int arg_count = 0;
	for (cdsl_arg_node_t* a = action->args; a; a = a->next) {
		int sub = 0;
		if (!compile_expr(a->expr, schema, bc, &sub)) {
			return 0;
		}
		if (sub + arg_count > arg_stack) {
			arg_stack = sub + arg_count;
		}
		arg_count++;
	}
	bc_emit_int(bc, arg_count);
	if (arg_count + 1 > arg_stack) {
		arg_stack = arg_count + 1;
	}
	if (!bc_ensure_cap(bc)) {
		return 0;
	}
	cdsl_value_t v = {.type = CDSL_TYPE_STRING, .data = {.string_val = action->action_name}};
	int idx = bc_add_constant(bc, v);
	bc_inst_t* inst = &bc->code[bc->count++];
	memset(inst, 0, sizeof(*inst));
	inst->op = BC_ACTION;
	inst->operand.const_idx = idx;
	if (arg_stack > *stack_used) {
		*stack_used = arg_stack;
	}
	return 1;
}

static cdsl_expr_node_t*
val_to_temp_expr(const cdsl_value_t* av)
{
	cdsl_expr_node_t* expr = calloc(1, sizeof(cdsl_expr_node_t));
	if (!expr) {
		return NULL;
	}
	switch (av->type) {
	case CDSL_TYPE_INT:
		expr->type = CDSL_EXPR_INT;
		expr->data.int_val = av->data.int_val;
		break;
	case CDSL_TYPE_FLOAT:
		expr->type = CDSL_EXPR_FLOAT;
		expr->data.float_val = av->data.float_val;
		break;
	case CDSL_TYPE_BOOL:
		expr->type = CDSL_EXPR_BOOL;
		expr->data.bool_val = av->data.bool_val;
		break;
	case CDSL_TYPE_DATE:
		expr->type = CDSL_EXPR_DATE;
		expr->data.date_val = av->data.date_val;
		break;
	case CDSL_TYPE_LONG:
		expr->type = CDSL_EXPR_LONG;
		expr->data.long_val = av->data.long_val;
		break;
	case CDSL_TYPE_STRING:
		expr->type = CDSL_EXPR_STRING;
		expr->data.string_val = cdsl_strdup(av->data.string_val ? av->data.string_val : "");
		break;
	case CDSL_TYPE_ARRAY: {
		expr->type = CDSL_EXPR_ARRAY;
		expr->data.array.elements = NULL;
		cdsl_arg_node_t* tail = NULL;
		if (av->data.array_val) {
			for (int j = 0; j < av->data.array_val->count; j++) {
				cdsl_arg_node_t* elem = calloc(1, sizeof(cdsl_arg_node_t));
				if (!elem) {
					break;
				}
				elem->expr = val_to_temp_expr(&av->data.array_val->items[j]);
				if (!tail) {
					expr->data.array.elements = elem;
				} else {
					tail->next = elem;
				}
				tail = elem;
			}
		}
		break;
	}
	default:
		expr->type = CDSL_EXPR_INT;
		expr->data.int_val = 0;
		break;
	}
	return expr;
}

int
cdsl_bytecode_compile(const cdsl_rule_t* rule, const cdsl_schema_t* schema, cdsl_bytecode_t* bc)
{
	if (!rule || !bc) {
		return 0;
	}
	memset(bc, 0, sizeof(*bc));
	bc->rule_name = cdsl_strdup(rule->name ? rule->name : "");
	char* rdesc = cdsl_meta_get(rule->meta_list, "description");
	bc->description = cdsl_strdup(rdesc ? rdesc : "");
	if (!bc->rule_name || !bc->description) {
		cdsl_bytecode_free(bc);
		return 0;
	}
	bc->pass_threshold = cdsl_meta_get_int(rule->meta_list, "pass_threshold", 80);
	bc->partial_threshold = cdsl_meta_get_int(rule->meta_list, "partial_threshold", 60);
	bc->is_metric_rule = (rule->metrics != NULL);

	int stack_used = 0;
	int compiled = 0;

	if (rule->metrics) {
		int mcount = 0;
		for (cdsl_metric_node_t* m = rule->metrics; m; m = m->next) {
			mcount++;
		}
		bc->metric_infos = calloc(mcount > 0 ? mcount : 1, sizeof(bc_metric_info_t));
		if (!bc->metric_infos) {
			cdsl_bytecode_free(bc);
			return 0;
		}
		bc->metric_info_count = mcount;

		int midx = 0;
		for (cdsl_metric_node_t* m = rule->metrics; m; m = m->next, midx++) {
			char* mdesc = cdsl_meta_get(m->meta_list, "description");
			int m_weight = cdsl_meta_get_int(m->meta_list, "weight", 0);
			if (bc->metric_infos) {
				bc->metric_infos[midx].name = cdsl_strdup(m->name ? m->name : "");
				bc->metric_infos[midx].description =
				    cdsl_strdup(mdesc ? mdesc : "");
				if (!bc->metric_infos[midx].name ||
				    !bc->metric_infos[midx].description) {
					cdsl_bytecode_free(bc);
					return 0;
				}
				bc->metric_infos[midx].max_weight = m_weight;
				bc->metric_infos[midx].is_critical =
				    cdsl_meta_get_bool(m->meta_list, "is_critical", 0);
			}

			bc_emit_metric_start(bc, m->name);
			int case_cap = 0;
			for (cdsl_case_node_t* c = m->case_list; c; c = c->next) {
				case_cap++;
			}
			int* metric_end_jmps = calloc(case_cap > 0 ? case_cap : 1, sizeof(int));
			if (!metric_end_jmps) {
				cdsl_bytecode_free(bc);
				return 0;
			}
			int njmps = 0;

			for (cdsl_case_node_t* c = m->case_list; c; c = c->next) {
				if (c->condition) {
					int sub = 0;
					compile_expr(c->condition, schema, bc, &sub);
					if (sub > stack_used) {
						stack_used = sub;
					}

					int next_case_jmp = bc_emit_jmp_placeholder(bc);
					bc_emit(bc, BC_POP); /* matched, discard condition */

					/* Compile action */
					if (c->action) {
						compile_action_call(
						    c->action, schema, bc, &stack_used);
						if (strcmp(c->action->action_name, "score") == 0 &&
						    c->action->args) {
							int asub = 0;
							compile_expr(c->action->args->expr,
								     schema,
								     bc,
								     &asub);
							if (asub > stack_used) {
								stack_used = asub;
							}
							bc_emit(bc, BC_SET_SCORE);
						} else if (strcmp(c->action->action_name,
								  "fail_metric") == 0) {
							bc_emit_int(bc, 0);
							bc_emit(bc, BC_SET_SCORE);
							if (c->action->args &&
							    c->action->args->next) {
								cdsl_expr_node_t* re =
								    c->action->args->next->expr;
								if (re &&
								    re->type == CDSL_EXPR_STRING) {
									bc_emit_fail_metric(
									    bc,
									    re->data.string_val);
								}
							}
						} else {
							bc_emit_int(bc, m_weight);
							bc_emit(bc, BC_SET_SCORE);
						}
					}

					if (metric_end_jmps && njmps < case_cap) {
						metric_end_jmps[njmps++] =
						    bc_emit_jmp_uncond_placeholder(bc);
					}
					bc_patch_jmp(bc, next_case_jmp, bc->count);
					bc_emit(bc, BC_POP); /* not matched, discard condition */
				}
			}

			/* Default action */
			if (m->default_action) {
				compile_action_call(m->default_action, schema, bc, &stack_used);
				if (strcmp(m->default_action->action_name, "score") == 0 &&
				    m->default_action->args) {
					int asub = 0;
					compile_expr(
					    m->default_action->args->expr, schema, bc, &asub);
					if (asub > stack_used) {
						stack_used = asub;
					}
					bc_emit(bc, BC_SET_SCORE);
				} else if (strcmp(m->default_action->action_name, "fail_metric") ==
					   0) {
					bc_emit_int(bc, 0);
					bc_emit(bc, BC_SET_SCORE);
					if (m->default_action->args &&
					    m->default_action->args->next) {
						cdsl_expr_node_t* re =
						    m->default_action->args->next->expr;
						if (re && re->type == CDSL_EXPR_STRING) {
							bc_emit_fail_metric(bc,
									    re->data.string_val);
						}
					}
				} else {
					bc_emit_int(bc, 0);
					bc_emit(bc, BC_SET_SCORE);
				}
			}

			/* Patch all jumps to end of metric */
			if (metric_end_jmps) {
				for (int i = 0; i < njmps; i++) {
					bc_patch_jmp(bc, metric_end_jmps[i], bc->count);
				}
				free(metric_end_jmps);
			}
		}
		bc_emit(bc, BC_RULE_END);
		compiled = 1;
	} else if (rule->when_expr) {
		int sub = 0;
		compile_expr(rule->when_expr, schema, bc, &sub);
		stack_used = sub;
		if (rule->then_action) {
			int false_jmp = bc_emit_jmp_placeholder(bc);
			compile_action_call(rule->then_action, schema, bc, &stack_used);
			bc_emit_bool(bc, 1);
			bc_patch_jmp(bc, false_jmp, bc->count);
		}
		bc_emit(bc, BC_RET);
		compiled = 1;
	}

	if (bc->max_stack < stack_used) {
		bc->max_stack = stack_used;
	}

	if (bc->max_stack > CDSL_BYTECODE_MAX_STACK) {
		cdsl_bytecode_free(bc);
		return 0;
	}

	return compiled;
}

/* ---- bytecode VM executor ---- */

static inline cdsl_value_t
bc_stack_underflow(cdsl_vm_t* vm)
{
	if (vm && !CDSL_ATOMIC_LOAD(&vm->error_state)) {
		cdsl_vm_set_last_error(
		    vm, CDSL_ERR_RUNTIME, 0, 0, "Bytecode stack underflow", NULL);
		CDSL_ATOMIC_STORE(&vm->error_state, 1);
	}
	cdsl_value_t void_val = {.type = CDSL_TYPE_VOID};
	return void_val;
}

int
bc_run_instructions(cdsl_vm_t* vm,
		    const cdsl_bytecode_t* bc,
		    cdsl_context_t* ctx,
		    bc_metric_handler_t metric_handler,
		    void* user_data,
		    cdsl_value_t* out_result)
{
	cdsl_value_t result = {.type = CDSL_TYPE_VOID, .data = {.int_val = 0}};
	double t0 = cdsl_get_time_us_internal();
	cdsl_value_t stack[CDSL_BYTECODE_MAX_STACK];
	int sp = 0;

#define PUSH(v)                                                                                    \
	do {                                                                                       \
		if (sp >= CDSL_BYTECODE_MAX_STACK) {                                               \
			cdsl_value_t _overflow_val = (v);                                          \
			cdsl_value_free(&_overflow_val);                                           \
			if (vm && !CDSL_ATOMIC_LOAD(&vm->error_state)) {                           \
				cdsl_vm_set_last_error(                                            \
				    vm, CDSL_ERR_RUNTIME, 0, 0, "Bytecode stack overflow", NULL);  \
				CDSL_ATOMIC_STORE(&vm->error_state, 1);                            \
			}                                                                          \
			result.type = CDSL_TYPE_VOID;                                              \
			goto done;                                                                 \
		}                                                                                  \
		stack[sp++] = (v);                                                                 \
	} while (0)
#define POP() (sp > 0 ? stack[--sp] : bc_stack_underflow(vm))
#define PEEK() (sp > 0 ? stack[sp - 1] : bc_stack_underflow(vm))

	const bc_inst_t* ip = bc->code;
	const bc_inst_t* end = bc->code + bc->count;
	int loop_cnt = 0;

	while (ip < end) {
		/* instruction count check */
		if (vm->instruction_limit > 0) {
			int64_t count = CDSL_ATOMIC_FETCH_ADD(&vm->instruction_count, 1) + 1;
			if (count > vm->instruction_limit) {
				cdsl_vm_set_last_error(
				    vm, CDSL_ERR_RUNTIME, 0, 0, "Instruction quota exceeded", NULL);
				CDSL_ATOMIC_STORE(&vm->error_state, 1);
				result.type = CDSL_TYPE_VOID;
				goto done;
			}
		}
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
			result.data.string_val =
			    bc->constants[ip->operand.const_idx].data.string_val;
			result.owned_string = 0;
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
		case BC_PUSH_ARRAY: {
			int count = ip->operand.int_val;
			cdsl_array_t* arr = cdsl_vm_calloc(vm, 1, sizeof(cdsl_array_t));
			if (arr) {
				arr->items = cdsl_vm_calloc(
				    vm, count > 0 ? (size_t)count : 1, sizeof(cdsl_value_t));
				if (!arr->items) {
					free(arr);
					arr = NULL;
				}
			}
			if (!arr) {
				for (int i = 0; i < count; i++) {
					cdsl_value_t tmp = POP();
					cdsl_value_free(&tmp);
				}
				if (!CDSL_ATOMIC_LOAD(&vm->error_state)) {
					cdsl_vm_set_last_error(vm,
							       CDSL_ERR_RUNTIME,
							       0,
							       0,
							       "Out of memory allocating array",
							       NULL);
					CDSL_ATOMIC_STORE(&vm->error_state, 1);
				}
				result.type = CDSL_TYPE_VOID;
				goto done;
			}
			arr->count = count;
			arr->capacity = count;
			for (int i = count - 1; i >= 0; i--) {
				arr->items[i] = POP();
			}
			result.type = CDSL_TYPE_ARRAY;
			result.data.array_val = arr;
			PUSH(result);
			ip++;
			break;
		}
		case BC_PUSH_VAR: {
			const char* var_name = bc->constants[ip->operand.const_idx].data.string_val;
			const cdsl_value_t* val_ptr = cdsl_context_lookup_value(ctx, var_name);
			if (val_ptr) {
				cdsl_value_t v = *val_ptr;
				if (v.type == CDSL_TYPE_STRING && v.data.string_val) {
					v.data.string_val = cdsl_vm_strdup(vm, v.data.string_val);
					v.owned_string = 1;
					if (!v.data.string_val) {
						v.type = CDSL_TYPE_VOID;
						CDSL_ATOMIC_STORE(&vm->error_state, 1);
						if (!vm->last_error) {
							cdsl_vm_set_last_error(
							    vm,
							    CDSL_ERR_RESOURCE_LIMIT,
							    0,
							    0,
							    "Out of memory duplicating string "
							    "variable in "
							    "bytecode",
							    NULL);
						}
						goto done;
					}
				} else if (v.type == CDSL_TYPE_ARRAY || v.type == CDSL_TYPE_MAP) {
					v = cdsl_value_copy(val_ptr);
				}
				PUSH(v);
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
				cdsl_value_t discarded = stack[--sp];
				cdsl_value_free(&discarded);
			}
			ip++;
			break;

		/* Binary Operators (Arithmetic & Relational) */
		case BC_ADD:
		case BC_SUB:
		case BC_MUL:
		case BC_DIV:
		case BC_EQ:
		case BC_NE:
		case BC_LT:
		case BC_GT:
		case BC_LE:
		case BC_GE:
		case BC_IN: {
			cdsl_value_t b = POP();
			cdsl_value_t a = POP();
			if (CDSL_ATOMIC_LOAD(&vm->error_state)) {
				cdsl_value_free(&a);
				cdsl_value_free(&b);
				result.type = CDSL_TYPE_VOID;
				goto done;
			}
			cdsl_op_t op = cdsl_bc_to_op(ip->op);
			cdsl_valop_status_t st = cdsl_value_binop(op, &a, &b, &result);
			cdsl_value_free(&a);
			cdsl_value_free(&b);
			if (st != CDSL_VALOP_OK && st != CDSL_VALOP_VOID_OPERAND) {
				if (!CDSL_ATOMIC_LOAD(&vm->error_state)) {
					cdsl_vm_set_last_error(vm,
							       CDSL_ERR_RUNTIME,
							       0,
							       0,
							       cdsl_valop_status_message(st),
							       cdsl_valop_status_hint(st));
					CDSL_ATOMIC_STORE(&vm->error_state, 1);
				}
				result.type = CDSL_TYPE_VOID;
				goto done;
			}
			PUSH(result);
			ip++;
			break;
		}

		case BC_IN_RANGE: {
			cdsl_value_t max_v = POP();
			cdsl_value_t min_v = POP();
			cdsl_value_t val = POP();
			if (CDSL_ATOMIC_LOAD(&vm->error_state)) {
				cdsl_value_free(&val);
				cdsl_value_free(&min_v);
				cdsl_value_free(&max_v);
				result.type = CDSL_TYPE_VOID;
				goto done;
			}
			cdsl_value_t ge_res, le_res;
			cdsl_valop_status_t st1 =
			    cdsl_value_binop(CDSL_OP_GE, &val, &min_v, &ge_res);
			cdsl_valop_status_t st2 =
			    cdsl_value_binop(CDSL_OP_LE, &val, &max_v, &le_res);
			result.type = CDSL_TYPE_BOOL;
			if (st1 == CDSL_VALOP_OK && st2 == CDSL_VALOP_OK) {
				result.data.bool_val =
				    (ge_res.data.bool_val && le_res.data.bool_val);
			} else {
				result.data.bool_val = 0;
			}
			cdsl_value_free(&val);
			cdsl_value_free(&min_v);
			cdsl_value_free(&max_v);
			PUSH(result);
			ip++;
			break;
		}

		case BC_BUILD_MAP: {
			int count = ip->operand.int_val;
			cdsl_map_t* m = cdsl_map_create(count > 0 ? count * 2 : 16);
			if (!m) {
				result.type = CDSL_TYPE_VOID;
				CDSL_ATOMIC_STORE(&vm->error_state, 1);
				goto done;
			}
			for (int i = count - 1; i >= 0; i--) {
				cdsl_value_t item_val = POP();
				cdsl_value_t key_val = POP();
				if (key_val.type == CDSL_TYPE_STRING && key_val.data.string_val) {
					cdsl_map_set(m, key_val.data.string_val, &item_val);
				}
				cdsl_value_free(&key_val);
				cdsl_value_free(&item_val);
			}
			result.type = CDSL_TYPE_MAP;
			result.data.map_val = m;
			PUSH(result);
			ip++;
			break;
		}

		case BC_GET_INDEX: {
			cdsl_value_t idx = POP();
			cdsl_value_t target = POP();
			result.type = CDSL_TYPE_VOID;
			if (target.type == CDSL_TYPE_MAP && target.data.map_val) {
				const char* k =
				    (idx.type == CDSL_TYPE_STRING) ? idx.data.string_val : NULL;
				if (k) {
					const cdsl_value_t* found =
					    cdsl_map_get(target.data.map_val, k);
					if (found) {
						result = cdsl_value_copy(found);
					}
				}
			} else if (target.type == CDSL_TYPE_ARRAY && target.data.array_val) {
				int i = (idx.type == CDSL_TYPE_INT)    ? idx.data.int_val
					: (idx.type == CDSL_TYPE_LONG) ? (int)idx.data.long_val
								       : -1;
				if (i >= 0 && i < target.data.array_val->count) {
					result = cdsl_value_copy(&target.data.array_val->items[i]);
				}
			}
			cdsl_value_free(&target);
			cdsl_value_free(&idx);
			PUSH(result);
			ip++;
			break;
		}

		case BC_GET_PROP: {
			const char* prop = bc->constants[ip->operand.const_idx].data.string_val;
			cdsl_value_t target = POP();
			result.type = CDSL_TYPE_VOID;
			if (target.type == CDSL_TYPE_MAP && target.data.map_val && prop) {
				const cdsl_value_t* found = cdsl_map_get(target.data.map_val, prop);
				if (found) {
					result = cdsl_value_copy(found);
				}
			}
			cdsl_value_free(&target);
			PUSH(result);
			ip++;
			break;
		}

		/* Unary Operators */
		case BC_NOT:
		case BC_NEG: {
			cdsl_value_t v = POP();
			if (CDSL_ATOMIC_LOAD(&vm->error_state)) {
				cdsl_value_free(&v);
				result.type = CDSL_TYPE_VOID;
				goto done;
			}
			cdsl_op_t op = cdsl_bc_to_op(ip->op);
			cdsl_valop_status_t st = cdsl_value_unop(op, &v, &result);
			cdsl_value_free(&v);
			if (st != CDSL_VALOP_OK && st != CDSL_VALOP_VOID_OPERAND) {
				if (!CDSL_ATOMIC_LOAD(&vm->error_state)) {
					cdsl_vm_set_last_error(vm,
							       CDSL_ERR_RUNTIME,
							       0,
							       0,
							       cdsl_valop_status_message(st),
							       cdsl_valop_status_hint(st));
					CDSL_ATOMIC_STORE(&vm->error_state, 1);
				}
				result.type = CDSL_TYPE_VOID;
				goto done;
			}
			PUSH(result);
			ip++;
			break;
		}

		case BC_CALL: {
			const char* fn_name = bc->constants[ip->operand.const_idx].data.string_val;
			cdsl_value_t nv = POP();
			int nargs = nv.data.int_val;
			/* Build arg list from stack (reverse since stack is LIFO) */
			cdsl_arg_node_t* arg_list = NULL;
			cdsl_arg_node_t* arg_nodes =
			    calloc(nargs > 0 ? nargs : 1, sizeof(cdsl_arg_node_t));
			if (!arg_nodes) {
				for (int i = 0; i < nargs; i++) {
					cdsl_value_t av = POP();
					cdsl_value_free(&av);
				}
				CDSL_ATOMIC_STORE(&vm->error_state, 1);
				if (!vm->last_error) {
					cdsl_vm_set_last_error(
					    vm,
					    CDSL_ERR_RESOURCE_LIMIT,
					    0,
					    0,
					    "Out of memory allocating builtin call arguments",
					    NULL);
				}
				result.type = CDSL_TYPE_VOID;
				goto done;
			}
			for (int i = nargs - 1; i >= 0; i--) {
				cdsl_value_t av = POP();
				// NOLINTNEXTLINE(clang-analyzer-security.ArrayBound)
				arg_nodes[i].expr = val_to_temp_expr(&av);
				cdsl_value_free(&av);
			}
			for (int i = 0; i < nargs; i++) {
				arg_nodes[i].next = (i + 1 < nargs) ? &arg_nodes[i + 1] : NULL;
			}
			arg_list = (nargs > 0) ? &arg_nodes[0] : NULL;

			result.type = CDSL_TYPE_VOID;
			cdsl_func_cb_t target_fn = NULL;
			CDSL_RWLOCK_RDLOCK(&vm->rwlock);
			for (cdsl_func_entry_t* fn = vm->functions; fn; fn = fn->next) {
				if (strcmp(fn->func_name, fn_name) == 0) {
					target_fn = fn->cb;
					break;
				}
			}
			CDSL_RWLOCK_UNLOCK_RD(&vm->rwlock);

			if (target_fn) {
				result = target_fn(fn_name, arg_list, ctx, vm);
			}

			for (int i = 0; i < nargs; i++) {
				cdsl_free_expr(arg_nodes[i].expr);
			}
			free(arg_nodes);
			PUSH(result);
			ip++;
			break;
		}

		case BC_ACTION: {
			const char* act_name = bc->constants[ip->operand.const_idx].data.string_val;
			cdsl_value_t nv = POP();
			int nargs = nv.data.int_val;
			cdsl_arg_node_t* arg_list = NULL;
			cdsl_arg_node_t* arg_nodes =
			    calloc(nargs > 0 ? nargs : 1, sizeof(cdsl_arg_node_t));
			if (!arg_nodes) {
				for (int i = 0; i < nargs; i++) {
					cdsl_value_t av = POP();
					cdsl_value_free(&av);
				}
				CDSL_ATOMIC_STORE(&vm->error_state, 1);
				if (!vm->last_error) {
					cdsl_vm_set_last_error(
					    vm,
					    CDSL_ERR_RESOURCE_LIMIT,
					    0,
					    0,
					    "Out of memory allocating action call arguments",
					    NULL);
				}
				result.type = CDSL_TYPE_VOID;
				goto done;
			}
			for (int i = nargs - 1; i >= 0; i--) {
				cdsl_value_t av = POP();
				// NOLINTNEXTLINE(clang-analyzer-security.ArrayBound)
				arg_nodes[i].expr = val_to_temp_expr(&av);
				cdsl_value_free(&av);
			}
			for (int i = 0; i < nargs; i++) {
				arg_nodes[i].next = (i + 1 < nargs) ? &arg_nodes[i + 1] : NULL;
			}
			arg_list = (nargs > 0) ? &arg_nodes[0] : NULL;

			cdsl_action_cb_t target_cb = NULL;
			CDSL_RWLOCK_RDLOCK(&vm->rwlock);
			for (cdsl_action_cb_entry_t* cb = vm->callbacks; cb; cb = cb->next) {
				if (strcmp(cb->action_name, act_name) == 0) {
					target_cb = cb->cb;
					break;
				}
			}
			CDSL_RWLOCK_UNLOCK_RD(&vm->rwlock);

			if (target_cb) {
				CDSL_ATOMIC_FETCH_ADD(&vm->stats.total_actions_triggered, 1);
				target_cb(act_name, arg_list, vm->user_data);
			}

			for (int i = 0; i < nargs; i++) {
				cdsl_free_expr(arg_nodes[i].expr);
			}
			free(arg_nodes);
			ip++;
			break;
		}

		case BC_DUP:
			if (sp <= 0) {
				bc_stack_underflow(vm);
				result.type = CDSL_TYPE_VOID;
				goto done;
			}
			{
				cdsl_value_t top = stack[sp - 1];
				if (top.type == CDSL_TYPE_STRING && top.data.string_val &&
				    top.owned_string) {
					top.data.string_val = cdsl_strdup(top.data.string_val);
					if (!top.data.string_val) {
						CDSL_ATOMIC_STORE(&vm->error_state, 1);
						cdsl_vm_set_last_error(
						    vm,
						    CDSL_ERR_RESOURCE_LIMIT,
						    0,
						    0,
						    "Out of memory duplicating string in BC_DUP",
						    NULL);
						goto done;
					}
				}
				PUSH(top);
			}
			ip++;
			break;

		case BC_JMP_IF_TRUE: {
			cdsl_value_t v = PEEK();
			if (v.data.bool_val) {
				ip += ip->operand.jump_offset;
			} else {
				cdsl_value_t discarded = POP();
				cdsl_value_free(&discarded);
				ip++;
			}
			break;
		}

		case BC_JMP_IF_FALSE: {
			cdsl_value_t v = PEEK();
			if (!v.data.bool_val) {
				ip += ip->operand.jump_offset;
			} else {
				cdsl_value_t discarded = POP();
				cdsl_value_free(&discarded);
				ip++;
			}
			break;
		}

		case BC_JMP:
			ip += ip->operand.jump_offset;
			break;

		case BC_METRIC_START:
		case BC_SET_SCORE:
		case BC_FAIL_METRIC:
		case BC_RULE_END: {
			/* Stack ops performed by the core loop; handler only
			   receives notifications + constants. */
			const cdsl_value_t* metric_val = NULL;
			cdsl_value_t popped = {.type = CDSL_TYPE_VOID};
			if (ip->op == BC_SET_SCORE) {
				popped = POP();
				metric_val = &popped;
			}
			if (metric_handler) {
				bc_metric_event_t ev;
				switch (ip->op) {
				case BC_METRIC_START:
					ev = BC_EV_METRIC_START;
					break;
				case BC_SET_SCORE:
					ev = BC_EV_SET_SCORE;
					break;
				case BC_FAIL_METRIC:
					ev = BC_EV_FAIL_METRIC;
					break;
				case BC_RULE_END:
					ev = BC_EV_RULE_END;
					break;
				default:
					ev = BC_EV_METRIC_START;
					break;
				}
				if (!metric_handler(ev, bc, ip, metric_val, user_data)) {
					cdsl_value_free(&popped);
					goto done;
				}
			}
			cdsl_value_free(&popped);
			ip++;
			break;
		}

		case BC_RET:
			result = POP();
			goto done;
		}
	}

done:
	while (sp > 0) {
		cdsl_value_t rem = stack[--sp];
		cdsl_value_free(&rem);
	}
	if (out_result) {
		*out_result = result;
	} else {
		cdsl_value_free(&result);
	}
#undef PUSH
#undef POP
#undef PEEK
	return 1;
}

/**
 * @brief Execute compiled bytecode against a context (public API).
 *
 * @param vm VM instance (for function/action dispatch)
 * @param bc Compiled bytecode chunk
 * @param ctx Execution context for variable lookups
 * @return Evaluated value; string pointers are borrowed from context/bytecode
 */
cdsl_value_t
cdsl_bytecode_execute(cdsl_vm_t* vm, const cdsl_bytecode_t* bc, cdsl_context_t* ctx)
{
	cdsl_value_t result = {.type = CDSL_TYPE_VOID, .data = {.int_val = 0}};
	if (!vm || !bc || !bc->code) {
		return result;
	}
	cdsl_vm_begin_execution(vm);
	cdsl_bc_run_threaded(vm, bc, ctx, NULL, NULL, &result);
	return result;
}

cdsl_value_t
cdsl_bytecode_execute_threaded(cdsl_vm_t* vm, const cdsl_bytecode_t* bc, cdsl_context_t* ctx)
{
	return cdsl_bytecode_execute(vm, bc, ctx);
}

typedef struct {
	cdsl_rule_report_t* report;
} bc_rule_state_t;

/**
 * @brief Build a simple-rule report from the final WHEN condition value.
 * @internal
 */
static void
bc_finalize_simple_rule_report(cdsl_rule_report_t* report, const cdsl_bytecode_t* bc, int when_true)
{
	report->metric_count = 1;
	report->metrics = calloc(1, sizeof(cdsl_metric_result_t));
	if (!report->metrics) {
		return;
	}
	cdsl_metric_result_t* m = &report->metrics[0];
	m->metric_name = cdsl_strdup(bc && bc->rule_name ? bc->rule_name : "");
	m->description = cdsl_strdup(bc && bc->description ? bc->description : "");
	m->max_weight = 100;
	m->score_obtained = when_true ? 0 : 100;
	m->is_passed = when_true ? 0 : 1;
	report->total_max_score = 100;
	report->total_obtained_score = when_true ? 0 : 100;
	report->status = when_true ? CDSL_STATUS_FAILED : CDSL_STATUS_PASSED;
	char buf[256];
	snprintf(buf,
		 sizeof(buf),
		 "%s (score: %d/100)",
		 when_true ? "FAILED" : "PASSED",
		 report->total_obtained_score);
	report->decision_summary = cdsl_strdup(buf);
	if (!m->metric_name || !m->description || !report->decision_summary) {
		report->status = CDSL_STATUS_ERROR;
	}
}

/**
 * @brief Metric handler for cdsl_bytecode_execute_rule.
 */
static int
bc_rule_metric_handler(bc_metric_event_t event,
		       const cdsl_bytecode_t* bc,
		       const bc_inst_t* inst,
		       const cdsl_value_t* value,
		       void* user_data)
{
	bc_rule_state_t* st = (bc_rule_state_t*)user_data;
	cdsl_rule_report_t* report = st->report;
	switch (event) {
	case BC_EV_METRIC_START: {
		const char* name = bc->constants[inst->operand.const_idx].data.string_val;
		int idx = report->metric_count;
		int new_count = idx + 1;
		cdsl_metric_result_t* new_metrics =
		    realloc(report->metrics, sizeof(cdsl_metric_result_t) * new_count);
		if (!new_metrics) {
			return 0; /* OOM -> abort */
		}
		report->metrics = new_metrics;
		cdsl_metric_result_t* mr = &report->metrics[idx];
		memset(mr, 0, sizeof(cdsl_metric_result_t));
		mr->metric_name = cdsl_strdup(name);
		if (bc->metric_infos && idx < bc->metric_info_count) {
			mr->description = cdsl_strdup(bc->metric_infos[idx].description
							  ? bc->metric_infos[idx].description
							  : "");
			mr->max_weight = bc->metric_infos[idx].max_weight;
			mr->is_critical = bc->metric_infos[idx].is_critical;
		} else {
			mr->description = cdsl_strdup("");
			mr->max_weight = 100;
			mr->is_critical = 0;
		}
		if (!mr->metric_name || !mr->description) {
			return 0;
		}
		report->metric_count = new_count;
		return 1;
	}
	case BC_EV_SET_SCORE: {
		if (report->metric_count == 0 || !value) {
			return 1;
		}
		cdsl_metric_result_t* mr = &report->metrics[report->metric_count - 1];
		mr->score_obtained = (value->type == CDSL_TYPE_INT) ? value->data.int_val : 0;
		mr->is_passed = (mr->score_obtained > 0);
		return 1;
	}
	case BC_EV_FAIL_METRIC: {
		if (report->metric_count == 0) {
			return 1;
		}
		cdsl_metric_result_t* mr = &report->metrics[report->metric_count - 1];
		const char* reason = bc->constants[inst->operand.const_idx].data.string_val;
		free(mr->violation_reason);
		mr->violation_reason = cdsl_strdup(reason);
		mr->score_obtained = 0;
		mr->is_passed = 0;
		return 1;
	}
	case BC_EV_RULE_END:
		return 0; /* abort instruction loop */
	default:
		return 1;
	}
}

cdsl_rule_report_t*
cdsl_bytecode_execute_rule(cdsl_vm_t* vm, const cdsl_bytecode_t* bc, cdsl_context_t* ctx)
{
	if (!vm || !bc || !bc->code || !ctx) {
		return NULL;
	}

	double t0 = cdsl_get_time_us_internal();
	cdsl_vm_begin_execution(vm);

	cdsl_rule_report_t* report = calloc(1, sizeof(*report));
	if (!report) {
		return NULL;
	}
	report->rule_name = cdsl_strdup(bc->rule_name ? bc->rule_name : "");
	report->description = cdsl_strdup(bc->description ? bc->description : "");
	if (!report->rule_name || !report->description) {
		CDSL_ATOMIC_STORE(&vm->error_state, 1);
		cdsl_vm_set_last_error(vm,
				       CDSL_ERR_RESOURCE_LIMIT,
				       0,
				       0,
				       "Out of memory allocating bytecode report metadata",
				       NULL);
		report->status = CDSL_STATUS_ERROR;
		report->error = vm->last_error;
		vm->last_error = NULL;
		return report;
	}

	bc_rule_state_t state;
	state.report = report;
	cdsl_value_t out_result = {.type = CDSL_TYPE_VOID};
	cdsl_bc_run_threaded(vm, bc, ctx, bc_rule_metric_handler, &state, &out_result);

	/* Check if aborted (instruction quota, timeout, etc.) */
	if (CDSL_ATOMIC_LOAD(&vm->error_state)) {
		report->status = CDSL_STATUS_ERROR;
		if (!report->decision_summary) {
			report->decision_summary = cdsl_strdup("ERROR");
		}
		if (report->metric_count == 0 && bc->metric_info_count == 0) {
			/* Simple rule: 1 metric representing the rule itself */
			report->metric_count = 1;
			report->metrics = calloc(1, sizeof(cdsl_metric_result_t));
			if (report->metrics) {
				report->metrics[0].metric_name =
				    cdsl_strdup(bc->rule_name ? bc->rule_name : "");
				report->metrics[0].description =
				    cdsl_strdup(bc->description ? bc->description : "");
			}
		}
		if (vm->last_error) {
			report->error = vm->last_error;
			vm->last_error = NULL;
		} else {
			report->error = cdsl_error_create(CDSL_ERR_RESOURCE_LIMIT,
							  0,
							  0,
							  "Execution failed due to resource limits",
							  NULL);
		}
		cdsl_value_free(&out_result);
	} else if (report->metric_count == 0 && out_result.type == CDSL_TYPE_BOOL) {
		/* For simple (WHEN/THEN) rules, no metric instruction is emitted and
		   the final BC_RET value is the WHEN condition result. Build a report
		   from it when no metric was recorded. */
		int when_true = out_result.data.bool_val;
		bc_finalize_simple_rule_report(report, bc, when_true);
		cdsl_value_free(&out_result);
	} else if (report->metric_count == 0) {
		/* No metric and no boolean WHEN result (should not happen for
		   well-formed rules); mark as ERROR. */
		report->status = CDSL_STATUS_ERROR;
		cdsl_value_free(&out_result);
	} else {
		cdsl_value_free(&out_result);

		/* Finalize metric rule report status */
		int total_max = 0, total_obtained = 0;
		int any_critical_failed = 0;
		for (int i = 0; i < report->metric_count; i++) {
			cdsl_metric_result_t* mr = &report->metrics[i];
			total_max += mr->max_weight;
			total_obtained += mr->score_obtained;
			if (!mr->is_passed && !mr->violation_reason) {
				mr->violation_reason = cdsl_strdup("default_condition_triggered");
			}
			if (mr->is_critical && !mr->is_passed) {
				any_critical_failed = 1;
			}
		}
		report->total_max_score = total_max;
		report->total_obtained_score = total_obtained;
		if (any_critical_failed) {
			report->status = CDSL_STATUS_FAILED;
		} else {
			int pass_thresh = bc->pass_threshold > 0 ? bc->pass_threshold : 80;
			int partial_thresh = bc->partial_threshold > 0 ? bc->partial_threshold : 60;
			if (total_obtained >= pass_thresh) {
				report->status = CDSL_STATUS_PASSED;
			} else if (total_obtained >= partial_thresh) {
				report->status = CDSL_STATUS_PARTIALLY_PASSED;
			} else {
				report->status = CDSL_STATUS_FAILED;
			}
		}

		char buf[512];
		switch (report->status) {
		case CDSL_STATUS_PASSED:
			snprintf(
			    buf, sizeof(buf), "PASSED (score: %d/%d)", total_obtained, total_max);
			break;
		case CDSL_STATUS_PARTIALLY_PASSED:
			snprintf(buf,
				 sizeof(buf),
				 "PARTIALLY PASSED (score: %d/%d, needs improvement)",
				 total_obtained,
				 total_max);
			break;
		case CDSL_STATUS_FAILED:
			snprintf(buf,
				 sizeof(buf),
				 "FAILED (score: %d/%d, critical items or threshold not met)",
				 total_obtained,
				 total_max);
			break;
		default:
			snprintf(buf, sizeof(buf), "ERROR");
			break;
		}
		report->decision_summary = cdsl_strdup(buf);
		if (!report->decision_summary) {
			report->status = CDSL_STATUS_ERROR;
		}
	}

	double elapsed = cdsl_get_time_us_internal() - t0;
	CDSL_RWLOCK_WRLOCK(&vm->rwlock);
	vm->stats.total_executions++;
	vm->stats.total_rules_executed++;
	vm->stats.total_time_us += elapsed;
	if (report) {
		vm->stats.total_metrics_evaluated += report->metric_count;
	}
	CDSL_RWLOCK_UNLOCK_WR(&vm->rwlock);

	return report;
}

void
cdsl_bytecode_free(cdsl_bytecode_t* bc)
{
	if (!bc) {
		return;
	}
	free(bc->code);
	for (int i = 0; i < bc->const_count; i++) {
		if (bc->constants[i].type == CDSL_TYPE_STRING) {
			free(bc->constants[i].data.string_val);
		}
	}
	free(bc->constants);
	free(bc->rule_name);
	free(bc->description);
	if (bc->metric_infos) {
		for (int i = 0; i < bc->metric_info_count; i++) {
			free(bc->metric_infos[i].name);
			free(bc->metric_infos[i].description);
		}
		free(bc->metric_infos);
	}
	memset(bc, 0, sizeof(*bc));
}
