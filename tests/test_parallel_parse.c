#include "ast.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define NUM_THREADS 10
#define NUM_ITERATIONS 100

const char* DSL_TEMPLATES[] = {
    "RULE r%d { WHEN user.age > %d THEN block(\"adult\") }",
    "RULE r%d { METRIC m1 { META { weight = \"100\" } CASE score > %d THEN score(100) DEFAULT "
    "score(0) } }",
    "TEMPLATE t%d { METRIC m1 { META { weight = \"50\" } CASE x == %d THEN score(50) DEFAULT "
    "score(0) } }",
};

void*
parse_worker(void* arg)
{
	int id = *(int*)arg;
	free(arg);

	for (int i = 0; i < NUM_ITERATIONS; i++) {
		char dsl[256];
		int template_idx = (id + i) % 3;
		sprintf(dsl, DSL_TEMPLATES[template_idx], id, i);

		cdsl_rule_t* rule = cdsl_parse_string(dsl);
		if (!rule) {
			fprintf(
			    stderr, "Thread %d: Failed to parse iteration %d: %s\n", id, i, dsl);
			exit(1);
		}

		// Verify name matches
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

int
main()
{
	pthread_t threads[NUM_THREADS];

	printf("Starting %d threads, each doing %d parses...\n", NUM_THREADS, NUM_ITERATIONS);

	for (int i = 0; i < NUM_THREADS; i++) {
		int* id = malloc(sizeof(int));
		*id = i;
		if (pthread_create(&threads[i], NULL, parse_worker, id) != 0) {
			perror("pthread_create");
			return 1;
		}
	}

	for (int i = 0; i < NUM_THREADS; i++) {
		pthread_join(threads[i], NULL);
	}

	printf("Parallel parsing test PASSED.\n");
	return 0;
}
