/**
 * @file cse.h
 * @brief Common Subexpression Elimination (CSE) and Shared Predicate Cache.
 *
 * Implements structural AST hashing, deep equality comparison, and an
 * execution-time predicate cache for sharing evaluated condition results
 * across rules within a ruleset or across evaluations with the same context.
 */

#ifndef CDSL_CSE_H
#define CDSL_CSE_H

#include "cdsl/ast.h"
#include "cdsl/context.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compute a 64-bit structural hash of an expression AST.
 *
 * Two ASTs with identical structure, operators, variable names, and literal values
 * will always produce the same hash value (FNV-1a 64-bit algorithm).
 *
 * @param expr Target expression AST node
 * @return 64-bit hash value, or 0 if expr is NULL
 */
uint64_t cdsl_expr_hash(const cdsl_expr_node_t* expr);

/**
 * @brief Check if two expression ASTs are structurally and semantically identical.
 *
 * @param a First expression AST node
 * @param b Second expression AST node
 * @return 1 if equal, 0 if not equal
 */
int cdsl_expr_equal(const cdsl_expr_node_t* a, const cdsl_expr_node_t* b);

/**
 * @brief An entry in the predicate cache.
 */
typedef struct cdsl_pred_cache_entry {
	uint64_t hash;
	const cdsl_expr_node_t* expr;
	cdsl_value_t value;
	struct cdsl_pred_cache_entry* next;
} cdsl_pred_cache_entry_t;

struct cdsl_pred_bound_ctx;

/**
 * @brief Predicate cache structure.
 */
typedef struct cdsl_pred_cache {
	cdsl_pred_cache_entry_t** buckets;
	size_t bucket_count;
	size_t count;
	int64_t hits;
	int64_t misses;
	struct cdsl_pred_bound_ctx* bound_contexts;
} cdsl_pred_cache_t;

/**
 * @brief Create a new predicate evaluation cache.
 * @param initial_buckets Number of hash buckets (0 for default 256)
 * @return Newly allocated cache (free with cdsl_pred_cache_free)
 */
cdsl_pred_cache_t* cdsl_pred_cache_create(size_t initial_buckets);

/**
 * @brief Free a predicate cache and all cached values.
 * @param cache Cache to free (NULL-safe)
 */
void cdsl_pred_cache_free(cdsl_pred_cache_t* cache);

/**
 * @brief Clear all cached entries in the predicate cache, resetting hits/misses.
 * @param cache Target cache
 */
void cdsl_pred_cache_clear(cdsl_pred_cache_t* cache);

/**
 * @brief Look up an expression result in the predicate cache.
 *
 * If found, increments cache->hits and stores a copy of the cached value in *out_val.
 *
 * @param cache Target cache
 * @param expr Expression to look up
 * @param out_val Pointer to receive a copy of the cached value
 * @return 1 if cache hit, 0 if cache miss
 */
int
cdsl_pred_cache_get(cdsl_pred_cache_t* cache, const cdsl_expr_node_t* expr, cdsl_value_t* out_val);

/**
 * @brief Store an evaluated expression result in the predicate cache.
 *
 * Makes a deep copy of the value.
 *
 * @param cache Target cache
 * @param expr Expression AST node
 * @param val Evaluated value
 * @return 1 on success, 0 on failure
 */
int cdsl_pred_cache_put(cdsl_pred_cache_t* cache,
			const cdsl_expr_node_t* expr,
			const cdsl_value_t* val);

/**
 * @brief Bind a predicate cache to a context for automatic mutation invalidation.
 *
 * Attaches an invalidation hook to the given evaluation context. Whenever any
 * variable within @p ctx is modified (via cdsl_context_set_* or JSON loading),
 * the predicate cache is automatically flushed (cleared) to prevent stale
 * subexpression evaluations across sequential rule evaluations.
 *
 * @note Invalidation & Thread-Safety Semantics:
 * - Mutation Invalidation: Any variable update in @p ctx triggers an immediate, full
 *   invalidation of all cached predicate expressions in @p cache.
 * - Thread Isolation: Both cdsl_pred_cache_t and cdsl_context_t hook callbacks are
 *   thread-confined (non-thread-safe without external synchronization).
 * - Parallel Ruleset Execution: In parallel ruleset execution routines
 *   (e.g., cdsl_vm_execute_ruleset_parallel), parent predicate caches are intentionally
 *   bypassed per-worker VM to avoid cross-thread cache races and lock contention.
 *   Predicate caching with context binding is fully active in sequential execution
 *   (cdsl_vm_execute_ruleset), where @p ctx is automatically bound to the execution
 *   cache for the duration of the ruleset.
 *
 * @param cache Predicate cache instance
 * @param ctx Context to listen to
 * @return 1 on success, 0 on failure (e.g., NULL pointers or out of memory)
 */
int cdsl_pred_cache_bind_context(cdsl_pred_cache_t* cache, cdsl_context_t* ctx);

/**
 * @brief Unbind a predicate cache from a context.
 * @param cache Predicate cache
 * @param ctx Context to unbind
 */
void cdsl_pred_cache_unbind_context(cdsl_pred_cache_t* cache, cdsl_context_t* ctx);

#ifdef __cplusplus
}
#endif

#endif /* CDSL_CSE_H */
