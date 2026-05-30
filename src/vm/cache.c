/**
 * @file vm_cache.c
 * @brief Compilation cache implementation.
 *
 * Provides thread-safe caching of parsed and verified DSL rules. The
 * cache uses a reader-writer lock for concurrent access and stores
 * compiled rule ASTs indexed by their DSL source text (used as key).
 *
 * Thread-safe design:
 * - Read lock for cache lookups (fast path, multiple readers)
 * - Release lock during parse/verify (slow path, no lock held)
 * - Write lock for cache insertion (single writer)
 * - Double-check pattern to avoid redundant compilation
 *
 * @defgroup cache Compilation Cache
 * @{
 */

#include "cdsl/execution.h"
#include "internal.h"
#include "cdsl/ast.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <pthread.h>

/**
 * @brief Helper to free a compiled rule entry (internal).
 *
 * Frees the cached rule AST, hash string, and the entry itself.
 * Used as the value destructor callback for the hash map.
 *
 * @param val Pointer to a cdsl_compiled_rule_t to free
 */
static void
free_compiled_rule(void* val)
{
	cdsl_compiled_rule_t* e = (cdsl_compiled_rule_t*)val;
	if (e) {
		cdsl_free_rule(e->rule);
		free(e->dsl_hash);
		free(e);
	}
}

cdsl_compile_cache_t*
cdsl_compile_cache_create(int capacity)
{
	cdsl_compile_cache_t* c = calloc(1, sizeof(*c));
	if (!c) {
		return NULL;
	}
	c->map = cdsl_hashmap_create(capacity > 0 ? capacity : 64);
	if (!c->map) {
		free(c);
		return NULL;
	}
	pthread_rwlock_init(&c->lock, NULL);
	return c;
}

void
cdsl_compile_cache_free(cdsl_compile_cache_t* cache)
{
	if (!cache) {
		return;
	}
	pthread_rwlock_wrlock(&cache->lock);
	cdsl_hashmap_free(cache->map, free_compiled_rule);
	pthread_rwlock_unlock(&cache->lock);
	pthread_rwlock_destroy(&cache->lock);
	free(cache);
}

cdsl_compiled_rule_t*
cdsl_compile(cdsl_compile_cache_t* cache,
	     const char* dsl_code,
	     const cdsl_schema_t* schema,
	     char* err_buf,
	     int err_buf_sz)
{
	if (!cache || !dsl_code) {
		if (err_buf) {
			snprintf(err_buf, err_buf_sz, "NULL cache or dsl_code");
		}
		return NULL;
	}

	/* 1. Fast path: Read lock for lookup */
	pthread_rwlock_rdlock(&cache->lock);
	cdsl_compiled_rule_t* existing =
	    (cdsl_compiled_rule_t*)cdsl_hashmap_get(cache->map, dsl_code);
	if (existing) {
		pthread_rwlock_unlock(&cache->lock);
		return existing;
	}
	pthread_rwlock_unlock(&cache->lock);

	/* 2. Slow path: Parse and verify outside lock */
	cdsl_rule_t* rule = cdsl_parse_string(dsl_code);
	if (!rule) {
		if (err_buf) {
			snprintf(err_buf, err_buf_sz, "Parse error");
		}
		return NULL;
	}
	if (schema) {
		char verr[512] = {0};
		if (!cdsl_verify_rule(rule, schema, verr, sizeof(verr))) {
			if (err_buf) {
				snprintf(err_buf, err_buf_sz, "Verify failed: %s", verr);
			}
			cdsl_free_rule(rule);
			return NULL;
		}
	}

	/* 3. Write path: Update cache */
	pthread_rwlock_wrlock(&cache->lock);
	existing = (cdsl_compiled_rule_t*)cdsl_hashmap_get(cache->map, dsl_code);
	if (existing) {
		/* Someone else compiled it while we were parsing */
		cdsl_free_rule(rule);
		pthread_rwlock_unlock(&cache->lock);
		return existing;
	}

	existing = calloc(1, sizeof(*existing));
	existing->rule = rule;
	existing->dsl_hash = strdup(dsl_code);
	existing->verified = 1;
	cdsl_hashmap_put(cache->map, dsl_code, existing);

	pthread_rwlock_unlock(&cache->lock);
	return existing;
}

cdsl_rule_report_t*
cdsl_vm_execute_compiled(cdsl_vm_t* vm, cdsl_compiled_rule_t* compiled, cdsl_context_t* ctx)
{
	if (!vm || !compiled || !compiled->rule || !ctx) {
		return NULL;
	}
	return cdsl_vm_execute(vm, compiled->rule, ctx);
}
/** @} */
