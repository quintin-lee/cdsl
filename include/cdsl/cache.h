/**
 * @file cdsl/cache.h
 * @brief Thread-safe compilation cache.
 *
 * @defgroup cdsl_cache Compilation Cache
 * @{
 */
#ifndef CDSL_CACHE_H
#define CDSL_CACHE_H

#include "cdsl/util/portability.h"

#include "cdsl/ast.h"
#include "cdsl/schema.h"
#include "cdsl/util/hashmap.h"
#include "cdsl/bytecode.h"
#include <pthread.h>

/**
 * @brief Handle to a compiled rule in the cache.
 */
typedef struct cdsl_compiled_rule {
	cdsl_rule_t* rule;
	char* dsl_hash;
	int verified;
	cdsl_bytecode_t bc;
} cdsl_compiled_rule_t;

/**
 * @brief Thread-safe compilation cache.
 */
typedef struct cdsl_compile_cache {
	cdsl_hashmap_t* map;
	pthread_rwlock_t lock;
} cdsl_compile_cache_t;

CDSL_NODISCARD
cdsl_compiled_rule_t* cdsl_compile(cdsl_compile_cache_t* cache,
				   const char* dsl_code,
				   const cdsl_schema_t* schema,
				   char* err_buf,
				   int err_buf_sz);
CDSL_NODISCARD
cdsl_compile_cache_t* cdsl_compile_cache_create(int capacity);
void cdsl_compile_cache_free(cdsl_compile_cache_t* cache);
/**
 * @brief Remove a compiled entry from the cache by DSL key.
 * @param cache Compilation cache
 * @param dsl_code DSL source string (must match exactly)
 * @return 1 if entry was found and removed, 0 if not found
 */
int cdsl_compile_cache_remove(cdsl_compile_cache_t* cache, const char* dsl_code);

#endif
/** @} */
