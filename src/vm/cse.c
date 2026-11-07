/**
 * @file src/vm/cse.c
 * @brief Common Subexpression Elimination (CSE) and Shared Predicate Cache.
 */

#include "cdsl/cse.h"
#include "cdsl/util/portability.h"
#include "internal.h"
#include <stdlib.h>
#include <string.h>

#define FNV1A_64_INIT 0xcbf29ce484222325ULL
#define FNV1A_64_PRIME 0x100000001b3ULL

static inline uint64_t
hash_byte(uint64_t h, uint8_t b)
{
	return (h ^ b) * FNV1A_64_PRIME;
}

static inline uint64_t
hash_bytes(uint64_t h, const void* ptr, size_t len)
{
	const uint8_t* p = (const uint8_t*)ptr;
	for (size_t i = 0; i < len; i++) {
		h = (h ^ p[i]) * FNV1A_64_PRIME;
	}
	return h;
}

static inline uint64_t
hash_str(uint64_t h, const char* s)
{
	if (!s) {
		return hash_byte(h, 0);
	}
	while (*s) {
		h = (h ^ (uint8_t)*s++) * FNV1A_64_PRIME;
	}
	return h;
}

static inline uint64_t
hash_u64(uint64_t h, uint64_t v)
{
	return hash_bytes(h, &v, sizeof(v));
}

uint64_t
cdsl_expr_hash(const cdsl_expr_node_t* expr)
{
	if (!expr) {
		return 0;
	}
	uint64_t h = hash_byte(FNV1A_64_INIT, (uint8_t)expr->type);
	switch (expr->type) {
	case CDSL_EXPR_ID:
		h = hash_str(h, expr->data.id_val);
		break;
	case CDSL_EXPR_INT:
		h = hash_u64(h, (uint64_t)(int64_t)expr->data.int_val);
		break;
	case CDSL_EXPR_LONG:
		h = hash_u64(h, (uint64_t)expr->data.long_val);
		break;
	case CDSL_EXPR_FLOAT: {
		double d = expr->data.float_val;
		if (d == 0.0) {
			d = 0.0;
		}
		uint64_t bits;
		memcpy(&bits, &d, sizeof(bits));
		h = hash_u64(h, bits);
		break;
	}
	case CDSL_EXPR_BOOL:
		h = hash_byte(h, expr->data.bool_val ? 1 : 0);
		break;
	case CDSL_EXPR_STRING:
		h = hash_str(h, expr->data.string_val);
		break;
	case CDSL_EXPR_DATE:
		h = hash_u64(h, (uint64_t)expr->data.date_val);
		break;
	case CDSL_EXPR_ARRAY:
		for (const cdsl_arg_node_t* a = expr->data.array.elements; a; a = a->next) {
			h = hash_u64(h, cdsl_expr_hash(a->expr));
		}
		break;
	case CDSL_EXPR_BINARY:
		h = hash_byte(h, (uint8_t)expr->data.binary.op);
		h = hash_u64(h, cdsl_expr_hash(expr->data.binary.left));
		h = hash_u64(h, cdsl_expr_hash(expr->data.binary.right));
		break;
	case CDSL_EXPR_UNARY:
		h = hash_byte(h, (uint8_t)expr->data.unary.op);
		h = hash_u64(h, cdsl_expr_hash(expr->data.unary.expr));
		break;
	case CDSL_EXPR_CALL:
		h = hash_str(h, expr->data.call.func_name);
		for (const cdsl_arg_node_t* a = expr->data.call.args; a; a = a->next) {
			h = hash_u64(h, cdsl_expr_hash(a->expr));
		}
		break;
	case CDSL_EXPR_MAP:
		for (const cdsl_map_ast_entry_t* m = expr->data.map.entries; m; m = m->next) {
			h = hash_str(h, m->key);
			h = hash_u64(h, cdsl_expr_hash(m->value));
		}
		break;
	case CDSL_EXPR_SUBSCRIPT:
		h = hash_u64(h, cdsl_expr_hash(expr->data.subscript.target));
		h = hash_u64(h, cdsl_expr_hash(expr->data.subscript.index));
		break;
	case CDSL_EXPR_RANGE:
		h = hash_u64(h, cdsl_expr_hash(expr->data.range.min_expr));
		h = hash_u64(h, cdsl_expr_hash(expr->data.range.max_expr));
		break;
	case CDSL_EXPR_LAMBDA:
		h = hash_str(h, expr->data.lambda.param_name);
		h = hash_u64(h, cdsl_expr_hash(expr->data.lambda.body));
		break;
	case CDSL_EXPR_ERROR:
		h = hash_byte(h, 0xEE);
		break;
	}
	return h;
}

int
cdsl_expr_equal(const cdsl_expr_node_t* a, const cdsl_expr_node_t* b)
{
	if (a == b) {
		return 1;
	}
	if (!a || !b) {
		return 0;
	}
	if (a->type != b->type) {
		return 0;
	}

	switch (a->type) {
	case CDSL_EXPR_ID:
		return (a->data.id_val && b->data.id_val &&
			strcmp(a->data.id_val, b->data.id_val) == 0);
	case CDSL_EXPR_INT:
		return a->data.int_val == b->data.int_val;
	case CDSL_EXPR_LONG:
		return a->data.long_val == b->data.long_val;
	case CDSL_EXPR_FLOAT:
		return a->data.float_val == b->data.float_val;
	case CDSL_EXPR_BOOL:
		return (a->data.bool_val != 0) == (b->data.bool_val != 0);
	case CDSL_EXPR_STRING:
		return (a->data.string_val && b->data.string_val &&
			strcmp(a->data.string_val, b->data.string_val) == 0);
	case CDSL_EXPR_DATE:
		return a->data.date_val == b->data.date_val;
	case CDSL_EXPR_ARRAY: {
		const cdsl_arg_node_t* ea = a->data.array.elements;
		const cdsl_arg_node_t* eb = b->data.array.elements;
		while (ea && eb) {
			if (!cdsl_expr_equal(ea->expr, eb->expr)) {
				return 0;
			}
			ea = ea->next;
			eb = eb->next;
		}
		return ea == NULL && eb == NULL;
	}
	case CDSL_EXPR_BINARY:
		return a->data.binary.op == b->data.binary.op &&
		       cdsl_expr_equal(a->data.binary.left, b->data.binary.left) &&
		       cdsl_expr_equal(a->data.binary.right, b->data.binary.right);
	case CDSL_EXPR_UNARY:
		return a->data.unary.op == b->data.unary.op &&
		       cdsl_expr_equal(a->data.unary.expr, b->data.unary.expr);
	case CDSL_EXPR_CALL: {
		if (!a->data.call.func_name || !b->data.call.func_name ||
		    strcmp(a->data.call.func_name, b->data.call.func_name) != 0) {
			return 0;
		}
		const cdsl_arg_node_t* ea = a->data.call.args;
		const cdsl_arg_node_t* eb = b->data.call.args;
		while (ea && eb) {
			if (!cdsl_expr_equal(ea->expr, eb->expr)) {
				return 0;
			}
			ea = ea->next;
			eb = eb->next;
		}
		return ea == NULL && eb == NULL;
	}
	case CDSL_EXPR_MAP: {
		if (a->data.map.count != b->data.map.count) {
			return 0;
		}
		const cdsl_map_ast_entry_t* ma = a->data.map.entries;
		const cdsl_map_ast_entry_t* mb = b->data.map.entries;
		while (ma && mb) {
			if (!ma->key || !mb->key || strcmp(ma->key, mb->key) != 0) {
				return 0;
			}
			if (!cdsl_expr_equal(ma->value, mb->value)) {
				return 0;
			}
			ma = ma->next;
			mb = mb->next;
		}
		return ma == NULL && mb == NULL;
	}
	case CDSL_EXPR_SUBSCRIPT:
		return cdsl_expr_equal(a->data.subscript.target, b->data.subscript.target) &&
		       cdsl_expr_equal(a->data.subscript.index, b->data.subscript.index);
	case CDSL_EXPR_RANGE:
		return cdsl_expr_equal(a->data.range.min_expr, b->data.range.min_expr) &&
		       cdsl_expr_equal(a->data.range.max_expr, b->data.range.max_expr);
	case CDSL_EXPR_LAMBDA:
		return a->data.lambda.param_name && b->data.lambda.param_name &&
		       strcmp(a->data.lambda.param_name, b->data.lambda.param_name) == 0 &&
		       cdsl_expr_equal(a->data.lambda.body, b->data.lambda.body);
	case CDSL_EXPR_ERROR:
		return 1;
	}
	return 0;
}

cdsl_pred_cache_t*
cdsl_pred_cache_create(size_t initial_buckets)
{
	cdsl_pred_cache_t* cache = calloc(1, sizeof(*cache));
	if (!cache) {
		return NULL;
	}
	cache->bucket_count = initial_buckets > 0 ? initial_buckets : 256;
	cache->buckets = calloc(cache->bucket_count, sizeof(cdsl_pred_cache_entry_t*));
	if (!cache->buckets) {
		free(cache);
		return NULL;
	}
	return cache;
}

void
cdsl_pred_cache_clear(cdsl_pred_cache_t* cache)
{
	if (!cache) {
		return;
	}
	for (size_t i = 0; i < cache->bucket_count; i++) {
		cdsl_pred_cache_entry_t* e = cache->buckets[i];
		while (e) {
			cdsl_pred_cache_entry_t* next = e->next;
			cdsl_value_free(&e->value);
			free(e);
			e = next;
		}
		cache->buckets[i] = NULL;
	}
	cache->count = 0;
	cache->hits = 0;
	cache->misses = 0;
}

void
cdsl_pred_cache_free(cdsl_pred_cache_t* cache)
{
	if (!cache) {
		return;
	}
	cdsl_pred_cache_clear(cache);
	free(cache->buckets);
	free(cache);
}

int
cdsl_pred_cache_get(cdsl_pred_cache_t* cache, const cdsl_expr_node_t* expr, cdsl_value_t* out_val)
{
	if (!cache || !expr || !out_val) {
		return 0;
	}
	uint64_t h = cdsl_expr_hash(expr);
	size_t bucket = (size_t)(h % cache->bucket_count);

	for (cdsl_pred_cache_entry_t* e = cache->buckets[bucket]; e; e = e->next) {
		if (e->hash == h && cdsl_expr_equal(e->expr, expr)) {
			cache->hits++;
			*out_val = cdsl_value_copy(&e->value);
			return 1;
		}
	}
	cache->misses++;
	return 0;
}

int
cdsl_pred_cache_put(cdsl_pred_cache_t* cache, const cdsl_expr_node_t* expr, const cdsl_value_t* val)
{
	if (!cache || !expr || !val) {
		return 0;
	}
	uint64_t h = cdsl_expr_hash(expr);
	size_t bucket = (size_t)(h % cache->bucket_count);

	for (cdsl_pred_cache_entry_t* e = cache->buckets[bucket]; e; e = e->next) {
		if (e->hash == h && cdsl_expr_equal(e->expr, expr)) {
			cdsl_value_free(&e->value);
			e->value = cdsl_value_copy(val);
			e->expr = expr;
			return 1;
		}
	}

	cdsl_pred_cache_entry_t* entry = calloc(1, sizeof(*entry));
	if (!entry) {
		return 0;
	}
	entry->hash = h;
	entry->expr = expr;
	entry->value = cdsl_value_copy(val);
	entry->next = cache->buckets[bucket];
	cache->buckets[bucket] = entry;
	cache->count++;
	return 1;
}
