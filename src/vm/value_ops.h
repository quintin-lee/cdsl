/**
 * @file src/vm/value_ops.h
 * @brief Shared unary and binary value semantics for AST eval, bytecode VM,
 *        and constant folding.
 */

#ifndef CDSL_VM_VALUE_OPS_H
#define CDSL_VM_VALUE_OPS_H

#include "cdsl/ast.h"
#include "cdsl/context.h"

/**
 * @brief Result status of a unary or binary value operation.
 */
typedef enum {
	CDSL_VALOP_OK = 0,	 /**< Operation succeeded; @c *out holds the result */
	CDSL_VALOP_VOID_OPERAND, /**< Operand was VOID (unset var / depth limit); non-fatal */
	CDSL_VALOP_TYPE_ERROR,	 /**< Incompatible operand types for operator */
	CDSL_VALOP_DIV_ZERO,	 /**< Division by zero */
	CDSL_VALOP_OVERFLOW	 /**< Signed integer or date arithmetic overflow */
} cdsl_valop_status_t;

/**
 * @brief Evaluate a non-short-circuit binary operator on two runtime values.
 *
 * Does NOT take ownership of @p a or @p b; caller remains responsible for
 * calling cdsl_value_free() on them.
 *
 * @param op  Operator (arithmetic or comparison; NOT CDSL_OP_AND / CDSL_OP_OR)
 * @param a   Left operand
 * @param b   Right operand
 * @param out Output value
 * @return    Status code
 */
cdsl_valop_status_t
cdsl_value_binop(cdsl_op_t op, const cdsl_value_t* a, const cdsl_value_t* b, cdsl_value_t* out);

/**
 * @brief Evaluate a unary operator (NOT or NEG) on a runtime value.
 *
 * @param op  CDSL_OP_NOT or CDSL_OP_NEG
 * @param v   Operand value
 * @param out Output value
 * @return    Status code
 */
cdsl_valop_status_t cdsl_value_unop(cdsl_op_t op, const cdsl_value_t* v, cdsl_value_t* out);

/**
 * @brief Human-readable runtime error message for a non-OK status.
 */
const char* cdsl_valop_status_message(cdsl_valop_status_t s);

/**
 * @brief Actionable hint string for a non-OK status (may be NULL).
 */
const char* cdsl_valop_status_hint(cdsl_valop_status_t s);

#endif
