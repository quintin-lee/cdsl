/**
 * @file src/vm/threaded.c
 * @brief Direct-threaded bytecode virtual machine interpreter.
 *
 * Implements direct-threaded code dispatch (labels as values / computed goto)
 * on GCC and Clang compilers, reducing CPU branch misprediction overhead on
 * inner execution loops. Falls back to standard switch execution on compilers
 * without computed goto support.
 */

#include "cdsl/bytecode.h"
#include "internal.h"
#include "value_ops.h"
#include <string.h>
#include <stdlib.h>

#if (defined(__GNUC__) || defined(__clang__)) && !defined(CDSL_NO_DIRECT_THREADING)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"

int
cdsl_bc_run_threaded(cdsl_vm_t* vm,
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
#define POP() (sp > 0 ? stack[--sp] : cdsl_bc_stack_underflow(vm))
#define PEEK() (sp > 0 ? stack[sp - 1] : cdsl_bc_stack_underflow(vm))

	const bc_inst_t* ip = bc->code;
	const bc_inst_t* end = bc->code + bc->count;
	int loop_cnt = 0;

	static const void* const dispatch_table[] = {
	    [BC_PUSH_INT] = &&DO_BC_PUSH_INT,
	    [BC_PUSH_FLOAT] = &&DO_BC_PUSH_FLOAT,
	    [BC_PUSH_BOOL] = &&DO_BC_PUSH_BOOL,
	    [BC_PUSH_STRING] = &&DO_BC_PUSH_STRING,
	    [BC_PUSH_DATE] = &&DO_BC_PUSH_DATE,
	    [BC_PUSH_LONG] = &&DO_BC_PUSH_LONG,
	    [BC_PUSH_ARRAY] = &&DO_BC_PUSH_ARRAY,
	    [BC_PUSH_VAR] = &&DO_BC_PUSH_VAR,
	    [BC_POP] = &&DO_BC_POP,
	    [BC_ADD] = &&DO_BC_ADD,
	    [BC_SUB] = &&DO_BC_SUB,
	    [BC_MUL] = &&DO_BC_MUL,
	    [BC_DIV] = &&DO_BC_DIV,
	    [BC_EQ] = &&DO_BC_EQ,
	    [BC_NE] = &&DO_BC_NE,
	    [BC_LT] = &&DO_BC_LT,
	    [BC_GT] = &&DO_BC_GT,
	    [BC_LE] = &&DO_BC_LE,
	    [BC_GE] = &&DO_BC_GE,
	    [BC_NOT] = &&DO_BC_NOT,
	    [BC_NEG] = &&DO_BC_NEG,
	    [BC_CALL] = &&DO_BC_CALL,
	    [BC_DUP] = &&DO_BC_DUP,
	    [BC_JMP_IF_FALSE] = &&DO_BC_JMP_IF_FALSE,
	    [BC_JMP_IF_TRUE] = &&DO_BC_JMP_IF_TRUE,
	    [BC_JMP] = &&DO_BC_JMP,
	    [BC_METRIC_START] = &&DO_BC_METRIC_START,
	    [BC_SET_SCORE] = &&DO_BC_SET_SCORE,
	    [BC_FAIL_METRIC] = &&DO_BC_FAIL_METRIC,
	    [BC_RULE_END] = &&DO_BC_RULE_END,
	    [BC_RET] = &&DO_BC_RET,
	    [BC_ACTION] = &&DO_BC_ACTION,
	    [BC_BUILD_MAP] = &&DO_BC_BUILD_MAP,
	    [BC_GET_PROP] = &&DO_BC_GET_PROP,
	    [BC_GET_INDEX] = &&DO_BC_GET_INDEX,
	    [BC_IN] = &&DO_BC_IN,
	    [BC_IN_RANGE] = &&DO_BC_IN_RANGE,
	};

#define DISPATCH()                                                                                 \
	do {                                                                                       \
		if (ip >= end)                                                                     \
			goto done;                                                                 \
		if (vm->instruction_limit > 0) {                                                   \
			int64_t _icnt = CDSL_ATOMIC_FETCH_ADD(&vm->instruction_count, 1) + 1;      \
			if (_icnt > vm->instruction_limit) {                                       \
				cdsl_vm_set_last_error(vm,                                         \
						       CDSL_ERR_RUNTIME,                           \
						       0,                                          \
						       0,                                          \
						       "Instruction quota exceeded",               \
						       NULL);                                      \
				CDSL_ATOMIC_STORE(&vm->error_state, 1);                            \
				result.type = CDSL_TYPE_VOID;                                      \
				goto done;                                                         \
			}                                                                          \
		}                                                                                  \
		if ((++loop_cnt & 0x3FF) == 0 && cdsl_vm_check_abort(vm, t0)) {                    \
			result.type = CDSL_TYPE_VOID;                                              \
			goto done;                                                                 \
		}                                                                                  \
		goto* dispatch_table[ip->op];                                                      \
	} while (0)

	DISPATCH();

#define OP_HANDLER(op) DO_##op
#define NEXT_INST()                                                                                \
	do {                                                                                       \
		ip++;                                                                              \
		DISPATCH();                                                                        \
	} while (0)
#define DISPATCH_NEXT() DISPATCH()
#include "instructions.inc"
#undef OP_HANDLER
#undef NEXT_INST
#undef DISPATCH_NEXT

done:
	while (sp > 0) {
		cdsl_value_t left_over = stack[--sp];
		cdsl_value_free(&left_over);
	}
	if (out_result) {
		*out_result = result;
	} else {
		cdsl_value_free(&result);
	}
#undef PUSH
#undef POP
#undef PEEK
#undef DISPATCH
	return 1;
}

#pragma GCC diagnostic pop
#else

int
cdsl_bc_run_threaded(cdsl_vm_t* vm,
		     const cdsl_bytecode_t* bc,
		     cdsl_context_t* ctx,
		     bc_metric_handler_t metric_handler,
		     void* user_data,
		     cdsl_value_t* out_result)
{
	return bc_run_instructions(vm, bc, ctx, metric_handler, user_data, out_result);
}

#endif
