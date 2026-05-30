/**
 * @file test_parallel_cache.c
 * @brief Stress test for thread-safe compilation cache.
 *
 * Spawns multiple threads that concurrently compile DSL rules
 * using the shared cdsl_compile_cache_t. Verifies that no
 * cache corruption or compilation errors occur under contention.
 * Each thread compiles 500 rules, cycling through a set of 32
 * unique rules to exercise both cache hits and misses.
 */

#include "cdsl/execution.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** @brief Number of concurrent threads. */
#define NUM_THREADS 8
/** @brief Number of compilation iterations per thread. */
#define NUM_ITERATIONS 500
/** @brief Size of the distinct rule set for cache cycling. */
#define CACHE_SIZE 32

/** @brief DSL templates for generating unique rules. */
const char* CACHE_DSL_TEMPLATES[] = {
    "RULE cache%d { WHEN user.age > %d THEN block(\"adult\") }",
    "RULE cache%d { METRIC m1 { META { weight = \"100\" } CASE score > %d THEN score(100) DEFAULT "
    "score(0) } }",
};

/**
 * @brief Worker argument for cache stress threads.
 */
typedef struct {
	cdsl_compile_cache_t* cache; /**< Shared compilation cache */
	int id;			     /**< Thread identifier */
} cache_worker_arg_t;

/**
 * @brief Thread worker: repeatedly compile rules, validating results.
 * @param arg cache_worker_arg_t pointer
 * @return NULL
 */
void*
cache_worker(void* arg)
{
	cache_worker_arg_t* warg = (cache_worker_arg_t*)arg;
	cdsl_compile_cache_t* cache = warg->cache;
	int id = warg->id;
	free(arg);

	for (int i = 0; i < NUM_ITERATIONS; i++) {
		char dsl[256];
		int rule_idx = i % CACHE_SIZE;
		sprintf(dsl, CACHE_DSL_TEMPLATES[rule_idx % 2], rule_idx, i);

		char err[256];
		cdsl_compiled_rule_t* compiled = cdsl_compile(cache, dsl, NULL, err, sizeof(err));
		if (!compiled) {
			fprintf(
			    stderr, "Thread %d: Failed to compile iteration %d: %s\n", id, i, err);
			exit(1);
		}

		/* Verify compiled rule integrity */
		if (!compiled->rule || !compiled->dsl_hash ||
		    strcmp(compiled->dsl_hash, dsl) != 0) {
			fprintf(stderr,
				"Thread %d: Cache corruption detected at iteration %d\n",
				id,
				i);
			exit(1);
		}
	}

	return NULL;
}

/**
 * @brief Main entry: run parallel cache stress test.
 * @return 0 on success, 1 on failure
 */
int
main()
{
	cdsl_compile_cache_t* cache = cdsl_compile_cache_create(CACHE_SIZE);
	pthread_t threads[NUM_THREADS];

	printf("Starting %d threads, each doing %d cache compilations on size %d...\n",
	       NUM_THREADS,
	       NUM_ITERATIONS,
	       CACHE_SIZE);

	for (int i = 0; i < NUM_THREADS; i++) {
		cache_worker_arg_t* id = malloc(sizeof(cache_worker_arg_t));
		id->cache = cache;
		id->id = i;
		if (pthread_create(&threads[i], NULL, cache_worker, id) != 0) {
			perror("pthread_create");
			return 1;
		}
	}

	for (int i = 0; i < NUM_THREADS; i++) {
		pthread_join(threads[i], NULL);
	}

	cdsl_compile_cache_free(cache);
	printf("Parallel cache test PASSED.\n");
	return 0;
}
