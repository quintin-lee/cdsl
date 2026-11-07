/**
 * @file src/vm/columnar.c
 * @brief Columnar data context and SIMD-vectorized batch rule evaluation.
 */

#include "cdsl/columnar.h"
#include "cdsl/util/portability.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>

cdsl_columnar_ctx_t*
cdsl_columnar_create(size_t row_count)
{
	cdsl_columnar_ctx_t* ctx = calloc(1, sizeof(*ctx));
	if (!ctx) {
		return NULL;
	}
	ctx->row_count = row_count;
	ctx->column_capacity = 8;
	ctx->columns = calloc(ctx->column_capacity, sizeof(cdsl_column_t*));
	if (!ctx->columns) {
		free(ctx);
		return NULL;
	}
	return ctx;
}

static void
column_free(cdsl_column_t* col)
{
	if (!col) {
		return;
	}
	if (col->name) {
		free(col->name);
	}
	if (col->owned) {
		if (col->type == CDSL_COL_STRING && col->values.string_data) {
			for (size_t i = 0; i < col->count; i++) {
				if (col->values.string_data[i]) {
					free((char*)col->values.string_data[i]);
				}
			}
			free(col->values.string_data);
		} else if (col->values.int_data) {
			free(col->values.int_data);
		}
	}
	free(col);
}

void
cdsl_columnar_free(cdsl_columnar_ctx_t* ctx)
{
	if (!ctx) {
		return;
	}
	for (int i = 0; i < ctx->column_count; i++) {
		column_free(ctx->columns[i]);
	}
	free(ctx->columns);
	free(ctx);
}

static int
columnar_add_column(cdsl_columnar_ctx_t* ctx, cdsl_column_t* col)
{
	if (!ctx || !col) {
		return 0;
	}
	if (ctx->column_count >= ctx->column_capacity) {
		int new_cap = ctx->column_capacity * 2;
		cdsl_column_t** new_cols = realloc(ctx->columns, sizeof(cdsl_column_t*) * new_cap);
		if (!new_cols) {
			return 0;
		}
		ctx->columns = new_cols;
		ctx->column_capacity = new_cap;
	}
	ctx->columns[ctx->column_count++] = col;
	return 1;
}

int
cdsl_columnar_add_int(cdsl_columnar_ctx_t* ctx, const char* name, const int32_t* data)
{
	if (!ctx || !name) {
		return 0;
	}
	cdsl_column_t* col = calloc(1, sizeof(*col));
	if (!col) {
		return 0;
	}
	col->name = cdsl_strdup(name);
	col->type = CDSL_COL_INT;
	col->count = ctx->row_count;
	col->owned = 1;
	col->values.int_data = calloc(ctx->row_count, sizeof(int32_t));
	if (!col->name || !col->values.int_data) {
		column_free(col);
		return 0;
	}
	if (data) {
		memcpy(col->values.int_data, data, sizeof(int32_t) * ctx->row_count);
	}
	if (!columnar_add_column(ctx, col)) {
		column_free(col);
		return 0;
	}
	return 1;
}

int
cdsl_columnar_add_long(cdsl_columnar_ctx_t* ctx, const char* name, const int64_t* data)
{
	if (!ctx || !name) {
		return 0;
	}
	cdsl_column_t* col = calloc(1, sizeof(*col));
	if (!col) {
		return 0;
	}
	col->name = cdsl_strdup(name);
	col->type = CDSL_COL_LONG;
	col->count = ctx->row_count;
	col->owned = 1;
	col->values.long_data = calloc(ctx->row_count, sizeof(int64_t));
	if (!col->name || !col->values.long_data) {
		column_free(col);
		return 0;
	}
	if (data) {
		memcpy(col->values.long_data, data, sizeof(int64_t) * ctx->row_count);
	}
	if (!columnar_add_column(ctx, col)) {
		column_free(col);
		return 0;
	}
	return 1;
}

int
cdsl_columnar_add_float(cdsl_columnar_ctx_t* ctx, const char* name, const double* data)
{
	if (!ctx || !name) {
		return 0;
	}
	cdsl_column_t* col = calloc(1, sizeof(*col));
	if (!col) {
		return 0;
	}
	col->name = cdsl_strdup(name);
	col->type = CDSL_COL_FLOAT;
	col->count = ctx->row_count;
	col->owned = 1;
	col->values.float_data = calloc(ctx->row_count, sizeof(double));
	if (!col->name || !col->values.float_data) {
		column_free(col);
		return 0;
	}
	if (data) {
		memcpy(col->values.float_data, data, sizeof(double) * ctx->row_count);
	}
	if (!columnar_add_column(ctx, col)) {
		column_free(col);
		return 0;
	}
	return 1;
}

int
cdsl_columnar_add_bool(cdsl_columnar_ctx_t* ctx, const char* name, const uint8_t* data)
{
	if (!ctx || !name) {
		return 0;
	}
	cdsl_column_t* col = calloc(1, sizeof(*col));
	if (!col) {
		return 0;
	}
	col->name = cdsl_strdup(name);
	col->type = CDSL_COL_BOOL;
	col->count = ctx->row_count;
	col->owned = 1;
	col->values.bool_data = calloc(ctx->row_count, sizeof(uint8_t));
	if (!col->name || !col->values.bool_data) {
		column_free(col);
		return 0;
	}
	if (data) {
		memcpy(col->values.bool_data, data, sizeof(uint8_t) * ctx->row_count);
	}
	if (!columnar_add_column(ctx, col)) {
		column_free(col);
		return 0;
	}
	return 1;
}

int
cdsl_columnar_add_string(cdsl_columnar_ctx_t* ctx, const char* name, const char** data)
{
	if (!ctx || !name) {
		return 0;
	}
	cdsl_column_t* col = calloc(1, sizeof(*col));
	if (!col) {
		return 0;
	}
	col->name = cdsl_strdup(name);
	col->type = CDSL_COL_STRING;
	col->count = ctx->row_count;
	col->owned = 1;
	col->values.string_data = calloc(ctx->row_count, sizeof(const char*));
	if (!col->name || !col->values.string_data) {
		column_free(col);
		return 0;
	}
	if (data) {
		for (size_t i = 0; i < ctx->row_count; i++) {
			col->values.string_data[i] = data[i] ? cdsl_strdup(data[i]) : NULL;
		}
	}
	if (!columnar_add_column(ctx, col)) {
		column_free(col);
		return 0;
	}
	return 1;
}

const cdsl_column_t*
cdsl_columnar_get(const cdsl_columnar_ctx_t* ctx, const char* name)
{
	if (!ctx || !name) {
		return NULL;
	}
	for (int i = 0; i < ctx->column_count; i++) {
		if (strcmp(ctx->columns[i]->name, name) == 0) {
			return ctx->columns[i];
		}
	}
	return NULL;
}

/* Vectorized operations */

static void
vector_cmp_int(
    const int32_t* restrict col, cdsl_op_t op, int32_t val, uint8_t* restrict out, size_t n)
{
	switch (op) {
	case CDSL_OP_EQ:
		for (size_t i = 0; i < n; i++) {
			out[i] = (col[i] == val);
		}
		break;
	case CDSL_OP_NE:
		for (size_t i = 0; i < n; i++) {
			out[i] = (col[i] != val);
		}
		break;
	case CDSL_OP_LT:
		for (size_t i = 0; i < n; i++) {
			out[i] = (col[i] < val);
		}
		break;
	case CDSL_OP_LE:
		for (size_t i = 0; i < n; i++) {
			out[i] = (col[i] <= val);
		}
		break;
	case CDSL_OP_GT:
		for (size_t i = 0; i < n; i++) {
			out[i] = (col[i] > val);
		}
		break;
	case CDSL_OP_GE:
		for (size_t i = 0; i < n; i++) {
			out[i] = (col[i] >= val);
		}
		break;
	default:
		memset(out, 0, n);
		break;
	}
}

static void
vector_cmp_long(
    const int64_t* restrict col, cdsl_op_t op, int64_t val, uint8_t* restrict out, size_t n)
{
	switch (op) {
	case CDSL_OP_EQ:
		for (size_t i = 0; i < n; i++) {
			out[i] = (col[i] == val);
		}
		break;
	case CDSL_OP_NE:
		for (size_t i = 0; i < n; i++) {
			out[i] = (col[i] != val);
		}
		break;
	case CDSL_OP_LT:
		for (size_t i = 0; i < n; i++) {
			out[i] = (col[i] < val);
		}
		break;
	case CDSL_OP_LE:
		for (size_t i = 0; i < n; i++) {
			out[i] = (col[i] <= val);
		}
		break;
	case CDSL_OP_GT:
		for (size_t i = 0; i < n; i++) {
			out[i] = (col[i] > val);
		}
		break;
	case CDSL_OP_GE:
		for (size_t i = 0; i < n; i++) {
			out[i] = (col[i] >= val);
		}
		break;
	default:
		memset(out, 0, n);
		break;
	}
}

static void
vector_cmp_float(
    const double* restrict col, cdsl_op_t op, double val, uint8_t* restrict out, size_t n)
{
	switch (op) {
	case CDSL_OP_EQ:
		for (size_t i = 0; i < n; i++) {
			out[i] = (col[i] == val);
		}
		break;
	case CDSL_OP_NE:
		for (size_t i = 0; i < n; i++) {
			out[i] = (col[i] != val);
		}
		break;
	case CDSL_OP_LT:
		for (size_t i = 0; i < n; i++) {
			out[i] = (col[i] < val);
		}
		break;
	case CDSL_OP_LE:
		for (size_t i = 0; i < n; i++) {
			out[i] = (col[i] <= val);
		}
		break;
	case CDSL_OP_GT:
		for (size_t i = 0; i < n; i++) {
			out[i] = (col[i] > val);
		}
		break;
	case CDSL_OP_GE:
		for (size_t i = 0; i < n; i++) {
			out[i] = (col[i] >= val);
		}
		break;
	default:
		memset(out, 0, n);
		break;
	}
}

static void
vector_cmp_string(
    const char** restrict col, cdsl_op_t op, const char* val, uint8_t* restrict out, size_t n)
{
	if (!val) {
		memset(out, 0, n);
		return;
	}
	if (op == CDSL_OP_EQ) {
		for (size_t i = 0; i < n; i++) {
			out[i] = (col[i] && strcmp(col[i], val) == 0);
		}
	} else if (op == CDSL_OP_NE) {
		for (size_t i = 0; i < n; i++) {
			out[i] = (!col[i] || strcmp(col[i], val) != 0);
		}
	} else {
		memset(out, 0, n);
	}
}

static int
eval_expr_vector(const cdsl_expr_node_t* expr, const cdsl_columnar_ctx_t* ctx, uint8_t* out)
{
	if (!expr || !ctx || !out) {
		return 0;
	}
	size_t n = ctx->row_count;

	if (expr->type == CDSL_EXPR_BOOL) {
		memset(out, expr->data.bool_val ? 1 : 0, n);
		return 1;
	}

	if (expr->type == CDSL_EXPR_UNARY && expr->data.unary.op == CDSL_OP_NOT) {
		if (!eval_expr_vector(expr->data.unary.expr, ctx, out)) {
			return 0;
		}
		for (size_t i = 0; i < n; i++) {
			out[i] = !out[i];
		}
		return 1;
	}

	if (expr->type == CDSL_EXPR_BINARY) {
		cdsl_op_t op = expr->data.binary.op;

		if (op == CDSL_OP_AND) {
			uint8_t* r_mask = malloc(n);
			if (!r_mask) {
				return 0;
			}
			if (!eval_expr_vector(expr->data.binary.left, ctx, out) ||
			    !eval_expr_vector(expr->data.binary.right, ctx, r_mask)) {
				free(r_mask);
				return 0;
			}
			for (size_t i = 0; i < n; i++) {
				out[i] &= r_mask[i];
			}
			free(r_mask);
			return 1;
		}

		if (op == CDSL_OP_OR) {
			uint8_t* r_mask = malloc(n);
			if (!r_mask) {
				return 0;
			}
			if (!eval_expr_vector(expr->data.binary.left, ctx, out) ||
			    !eval_expr_vector(expr->data.binary.right, ctx, r_mask)) {
				free(r_mask);
				return 0;
			}
			for (size_t i = 0; i < n; i++) {
				out[i] |= r_mask[i];
			}
			free(r_mask);
			return 1;
		}

		/* Interval Range: col in [min..max] */
		if (op == CDSL_OP_IN && expr->data.binary.right->type == CDSL_EXPR_RANGE) {
			cdsl_expr_node_t* left = expr->data.binary.left;
			cdsl_expr_node_t* rng = expr->data.binary.right;
			if (left->type == CDSL_EXPR_ID && rng->data.range.min_expr &&
			    rng->data.range.max_expr) {
				const cdsl_column_t* col =
				    cdsl_columnar_get(ctx, left->data.id_val);
				if (col && col->type == CDSL_COL_INT &&
				    rng->data.range.min_expr->type == CDSL_EXPR_INT &&
				    rng->data.range.max_expr->type == CDSL_EXPR_INT) {
					int32_t min_v = rng->data.range.min_expr->data.int_val;
					int32_t max_v = rng->data.range.max_expr->data.int_val;
					const int32_t* restrict vals = col->values.int_data;
					for (size_t i = 0; i < n; i++) {
						out[i] = (vals[i] >= min_v && vals[i] <= max_v);
					}
					return 1;
				}
			}
		}

		/* Relational: col <op> literal (or literal <op> col) */
		cdsl_expr_node_t* left = expr->data.binary.left;
		cdsl_expr_node_t* right = expr->data.binary.right;

		if (left->type != CDSL_EXPR_ID && right->type == CDSL_EXPR_ID) {
			cdsl_expr_node_t* tmp = left;
			left = right;
			right = tmp;
			switch (op) {
			case CDSL_OP_LT:
				op = CDSL_OP_GT;
				break;
			case CDSL_OP_LE:
				op = CDSL_OP_GE;
				break;
			case CDSL_OP_GT:
				op = CDSL_OP_LT;
				break;
			case CDSL_OP_GE:
				op = CDSL_OP_LE;
				break;
			default:
				break;
			}
		}

		if (left->type == CDSL_EXPR_ID) {
			const cdsl_column_t* col = cdsl_columnar_get(ctx, left->data.id_val);
			if (col) {
				if (col->type == CDSL_COL_INT && right->type == CDSL_EXPR_INT) {
					vector_cmp_int(
					    col->values.int_data, op, right->data.int_val, out, n);
					return 1;
				}
				if (col->type == CDSL_COL_LONG && right->type == CDSL_EXPR_INT) {
					vector_cmp_long(col->values.long_data,
							op,
							(int64_t)right->data.int_val,
							out,
							n);
					return 1;
				}
				if (col->type == CDSL_COL_FLOAT && right->type == CDSL_EXPR_FLOAT) {
					vector_cmp_float(col->values.float_data,
							 op,
							 right->data.float_val,
							 out,
							 n);
					return 1;
				}
				if (col->type == CDSL_COL_FLOAT && right->type == CDSL_EXPR_INT) {
					vector_cmp_float(col->values.float_data,
							 op,
							 (double)right->data.int_val,
							 out,
							 n);
					return 1;
				}
				if (col->type == CDSL_COL_STRING &&
				    right->type == CDSL_EXPR_STRING) {
					vector_cmp_string(col->values.string_data,
							  op,
							  right->data.string_val,
							  out,
							  n);
					return 1;
				}
				if (col->type == CDSL_COL_BOOL && right->type == CDSL_EXPR_BOOL) {
					uint8_t bv = right->data.bool_val ? 1 : 0;
					const uint8_t* restrict cvals = col->values.bool_data;
					if (op == CDSL_OP_EQ) {
						for (size_t i = 0; i < n; i++) {
							out[i] = (cvals[i] == bv);
						}
					} else if (op == CDSL_OP_NE) {
						for (size_t i = 0; i < n; i++) {
							out[i] = (cvals[i] != bv);
						}
					}
					return 1;
				}
			}
		}
	}

	return 0;
}

int64_t
cdsl_columnar_eval_rule(const cdsl_rule_t* rule,
			const cdsl_columnar_ctx_t* ctx,
			uint8_t* out_matches)
{
	if (!rule || !ctx || !out_matches || ctx->row_count == 0) {
		return -1;
	}
	if (!rule->when_expr) {
		memset(out_matches, 1, ctx->row_count);
		return (int64_t)ctx->row_count;
	}

	if (!eval_expr_vector(rule->when_expr, ctx, out_matches)) {
		return -1;
	}

	int64_t matches = 0;
	for (size_t i = 0; i < ctx->row_count; i++) {
		matches += out_matches[i];
	}
	return matches;
}
