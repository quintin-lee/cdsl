/**
 * @file columnar.h
 * @brief Columnar data context and SIMD-vectorized batch rule evaluation.
 *
 * Provides high-throughput batch evaluation across tables/arrays of records.
 * Conditions are evaluated in contiguous columnar memory loops that auto-vectorize
 * with SIMD (AVX2/AVX-512/NEON), evaluating tens of millions of records per second.
 */

#ifndef CDSL_COLUMNAR_H
#define CDSL_COLUMNAR_H

#include "cdsl/ast.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Data types supported in columnar storage.
 */
typedef enum {
	CDSL_COL_INT,	/**< 32-bit signed integer (int32_t) */
	CDSL_COL_LONG,	/**< 64-bit signed integer (int64_t) */
	CDSL_COL_FLOAT, /**< 64-bit floating point (double) */
	CDSL_COL_BOOL,	/**< 8-bit boolean (0 or 1) */
	CDSL_COL_STRING /**< Null-terminated C string pointer */
} cdsl_col_type_t;

/**
 * @brief Contiguous single column data vector.
 */
typedef struct cdsl_column {
	char* name;
	cdsl_col_type_t type;
	size_t count;
	union {
		int32_t* int_data;
		int64_t* long_data;
		double* float_data;
		uint8_t* bool_data;
		const char** string_data;
	} values;
	int owned;
} cdsl_column_t;

/**
 * @brief Columnar context storing a batch of named columns.
 */
typedef struct cdsl_columnar_ctx {
	size_t row_count;
	cdsl_column_t** columns;
	int column_count;
	int column_capacity;
} cdsl_columnar_ctx_t;

/**
 * @brief Create a new columnar batch context with a fixed row count.
 * @param row_count Number of rows in the batch
 * @return Newly allocated columnar context (free with cdsl_columnar_free)
 */
cdsl_columnar_ctx_t* cdsl_columnar_create(size_t row_count);

/**
 * @brief Free a columnar batch context and all owned columns.
 * @param ctx Context to free (NULL-safe)
 */
void cdsl_columnar_free(cdsl_columnar_ctx_t* ctx);

/**
 * @brief Add an int32 column to the context.
 * @param ctx Context
 * @param name Column name (e.g. "age" or "user.age")
 * @param data Array of count int32_t elements (copied internally)
 * @return 1 on success, 0 on failure
 */
int cdsl_columnar_add_int(cdsl_columnar_ctx_t* ctx, const char* name, const int32_t* data);

/**
 * @brief Add an int64 column to the context.
 * @param ctx Context
 * @param name Column name
 * @param data Array of count int64_t elements (copied internally)
 * @return 1 on success, 0 on failure
 */
int cdsl_columnar_add_long(cdsl_columnar_ctx_t* ctx, const char* name, const int64_t* data);

/**
 * @brief Add a double column to the context.
 * @param ctx Context
 * @param name Column name
 * @param data Array of count double elements (copied internally)
 * @return 1 on success, 0 on failure
 */
int cdsl_columnar_add_float(cdsl_columnar_ctx_t* ctx, const char* name, const double* data);

/**
 * @brief Add a boolean column to the context.
 * @param ctx Context
 * @param name Column name
 * @param data Array of count uint8_t elements (0 or 1, copied internally)
 * @return 1 on success, 0 on failure
 */
int cdsl_columnar_add_bool(cdsl_columnar_ctx_t* ctx, const char* name, const uint8_t* data);

/**
 * @brief Add a string column to the context.
 * @param ctx Context
 * @param name Column name
 * @param data Array of count const char* pointers (strings are duplicated internally)
 * @return 1 on success, 0 on failure
 */
int cdsl_columnar_add_string(cdsl_columnar_ctx_t* ctx, const char* name, const char** data);

/**
 * @brief Find a column by name.
 * @param ctx Context
 * @param name Column name
 * @return Pointer to column or NULL if not found
 */
const cdsl_column_t* cdsl_columnar_get(const cdsl_columnar_ctx_t* ctx, const char* name);

/**
 * @brief Evaluate a rule's condition across all rows in the columnar context using SIMD batch execution.
 *
 * For each row $i$, sets `out_matches[i] = 1` if the WHEN condition is true (violation detected),
 * or `0` if false.
 *
 * @param rule Target rule
 * @param ctx Columnar context
 * @param out_matches Output array of uint8_t with size at least `ctx->row_count`
 * @return Total number of matching rows, or -1 on error
 */
int64_t cdsl_columnar_eval_rule(const cdsl_rule_t* rule,
				const cdsl_columnar_ctx_t* ctx,
				uint8_t* out_matches);

#ifdef __cplusplus
}
#endif

#endif /* CDSL_COLUMNAR_H */
