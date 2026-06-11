/**
 * @file test_parallel_parse.c
 * @brief Stress test for thread-safe DSL parsing with reentrant lexer.
 *
 * Spawns multiple threads that concurrently parse DSL rule strings
 * using the reentrant Flex/Bison parser. Verifies that no parse
 * errors or cross-thread corruption occur under contention.
 * Each thread parses 100 rules including simple rules, metric rules,
 * and template definitions.
 */

#include "cdsl/ast.h"
#include "cdsl/util/threads.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/** @brief Number of concurrent threads. */
#define NUM_THREADS 10
/** @brief Number of parse iterations per thread. */
#define NUM_ITERATIONS 100

/** @brief DSL templates for generating diverse parse inputs. */
const char* DSL_TEMPLATES[] = {
    "RULE r%d { WHEN user.age > %d THEN block(\"adult\") }",
    "RULE r%d { METRIC m1 { META { weight = \"100\" } CASE score > %d THEN score(100) DEFAULT "
    "score(0) } }",
    "TEMPLATE t%d { METRIC m1 { META { weight = \"50\" } CASE x == %d THEN score(50) DEFAULT "
    "score(0) } }",
};

/**
 * @brief Thread worker: repeatedly parse DSL strings, validating results.
 * @param arg Integer pointer to thread ID (freed by worker)
 * @return NULL
 */
void*
parse_worker(void* arg)
{
	int id = *(int*)arg;
	free(arg);

	for (int i = 0; i < NUM_ITERATIONS; i++) {
		char dsl[256];
		int template_idx = (id + i) % 3;
		_Pragma("GCC diagnostic push")
		    _Pragma("GCC diagnostic ignored \"-Wformat-nonliteral\"")
			sprintf(dsl, DSL_TEMPLATES[template_idx], id, i);
		_Pragma("GCC diagnostic pop")

		    cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
		if (!rule) {
			fprintf(
			    stderr, "Thread %d: Failed to parse iteration %d: %s\n", id, i, dsl);
			exit(1);
		}

		/* Verify rule name matches expected pattern */
		char expected_name[32];
		if (template_idx == 2) {
			sprintf(expected_name, "t%d", id);
		} else {
			sprintf(expected_name, "r%d", id);
		}

		if (strcmp(rule->name, expected_name) != 0) {
			fprintf(stderr,
				"Thread %d: Name mismatch. Expected %s, got %s\n",
				id,
				expected_name,
				rule->name);
			exit(1);
		}

		cdsl_free_rule(rule);
	}

	return NULL;
}

/**
 * @brief Main entry: run parallel parsing stress test.
 * @return 0 on success, 1 on failure
 */
int
main()
{
	cdsl_thread_t threads[NUM_THREADS];

	printf("Starting %d threads, each doing %d parses...\n", NUM_THREADS, NUM_ITERATIONS);

	for (int i = 0; i < NUM_THREADS; i++) {
		int* id = malloc(sizeof(int));
		*id = i;
		if (CDSL_THREAD_CREATE(&threads[i], parse_worker, id) != 0) {
			perror("pthread_create");
			return 1;
		}
	}

	for (int i = 0; i < NUM_THREADS; i++) {
		CDSL_THREAD_JOIN(threads[i]);
	}

	printf("Parallel parsing test PASSED.\n");
	return 0;
}
