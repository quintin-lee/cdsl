/**
 * @file cdsl/bytecode.h
 * @brief Bytecode VM: compile AST to linear bytecode, execute via stack VM.
 *
 * Replace recursive tree-walk interpretation with a flat instruction
 * stream for better cache locality, reduced function call overhead,
 * and simpler execution flow.
 *
 * @defgroup cdsl_bytecode Bytecode VM
 * @{
 */
#ifndef CDSL_BYTECODE_H
#define CDSL_BYTECODE_H

#include "cdsl/ast.h"
#include "cdsl/schema.h"
#include "cdsl/context.h"
#include "cdsl/vm.h"
#include "cdsl/report.h"

/** Maximum value stack depth for bytecode execution. */
#define CDSL_BYTECODE_MAX_STACK 256

/**
 * @brief Bytecode instruction opcodes.
 */
typedef enum {
	BC_PUSH_INT,	 /**< Push int literal [operand: int_val] */
	BC_PUSH_FLOAT,	 /**< Push float literal [operand: float_val] */
	BC_PUSH_BOOL,	 /**< Push bool literal [operand: bool_val] */
	BC_PUSH_STRING,	 /**< Push string literal [operand: string_val — borrowed] */
	BC_PUSH_DATE,	 /**< Push date literal [operand: date_val] */
	BC_PUSH_LONG,	 /**< Push long literal [operand: long_val] */
	BC_PUSH_VAR,	 /**< Load context variable [operand: string_val — name] */
	BC_POP,		 /**< Discard top of stack */
	BC_ADD,		 /**< Pop b, pop a, push a+b */
	BC_SUB,		 /**< Pop b, pop a, push a-b */
	BC_MUL,		 /**< Pop b, pop a, push a*b */
	BC_DIV,		 /**< Pop b, pop a, push a/b (void on /0) */
	BC_EQ,		 /**< Pop b, pop a, push a==b */
	BC_NE,		 /**< Pop b, pop a, push a!=b */
	BC_LT,		 /**< Pop b, pop a, push a<b */
	BC_GT,		 /**< Pop b, pop a, push a>b */
	BC_LE,		 /**< Pop b, pop a, push a<=b */
	BC_GE,		 /**< Pop b, pop a, push a>=b */
	BC_NOT,		 /**< Pop, push !a */
	BC_NEG,		 /**< Pop, push -a */
	BC_CALL,	 /**< Call built-in by name [operand: string_val] */
	BC_DUP,		 /**< Duplicate top of stack */
	BC_JMP_IF_FALSE, /**< Peek; jump [operand: jump_offset] if top is false */
	BC_JMP_IF_TRUE,	 /**< Peek; jump [operand: jump_offset] if top is true */
	BC_JMP,		 /**< Unconditional jump [operand: jump_offset] */
	BC_METRIC_START, /**< Start new metric [operand: const_idx (name)] */
	BC_SET_SCORE,	 /**< Set score for current metric [operand: none (pops value)] */
	BC_FAIL_METRIC,	 /**< Fail metric with reason [operand: const_idx (reason)] */
	BC_RULE_END,	 /**< Finalize rule report [operand: none] */
	BC_RET		 /**< Return (top of stack is result) */
} bc_op_t;

/**
 * @brief Single bytecode instruction.
 */
typedef struct {
	bc_op_t op;
	union {
		int int_val;
		double float_val;
		int bool_val;
		int const_idx; /**< Index into constant pool */
		time_t date_val;
		int64_t long_val;
		int jump_offset;
	} operand;
} bc_inst_t;

/**
 * @brief Compiled bytecode chunk for a single expression or rule.
 */
typedef struct {
	bc_inst_t* code;
	int count;
	int capacity;
	int max_stack;
	cdsl_value_t* constants; /**< Constant pool for strings and large literals */
	int const_count;
	int const_capacity;
} cdsl_bytecode_t;

/**
 * @brief Compile a rule AST into bytecode.
 *
 * Walks the AST and emits an instruction stream. Performs constant
 * folding where possible (e.g., 2+3 becomes a single BC_PUSH_INT 5).
 *
 * @param rule Parsed AST rule
 * @param schema Schema for type resolution (may be NULL)
 * @param bc Output bytecode chunk (must be zero-initialized)
 * @return 1 on success, 0 on failure
 */
int
cdsl_bytecode_compile(const cdsl_rule_t* rule, const cdsl_schema_t* schema, cdsl_bytecode_t* bc);

/**
 * @brief Execute compiled bytecode against a context.
 *
 * @param vm VM instance (for function/action dispatch)
 * @param bc Compiled bytecode chunk
 * @param ctx Execution context for variable lookups
 * @return Evaluated value; string pointers are borrowed from context/bytecode
 */
cdsl_value_t cdsl_bytecode_execute(cdsl_vm_t* vm, const cdsl_bytecode_t* bc, cdsl_context_t* ctx);

/**
 * @brief Execute bytecode to produce a full rule report.
 *
 * This function handles the logic for both Simple Rules and Metric Rules,
 * including score calculation, threshold checks, and action triggering.
 *
 * @param vm VM instance
 * @param bc Compiled bytecode chunk
 * @param ctx Execution context
 * @return Newly allocated rule report (must be freed with cdsl_report_free)
 */
cdsl_rule_report_t*
cdsl_bytecode_execute_rule(cdsl_vm_t* vm, const cdsl_bytecode_t* bc, cdsl_context_t* ctx);

/**
 * @brief Free a bytecode chunk.
 * @param bc Bytecode chunk to free (NULL-safe)
 */
void cdsl_bytecode_free(cdsl_bytecode_t* bc);

#endif
/** @} */
