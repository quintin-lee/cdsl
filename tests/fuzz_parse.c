/**
 * @file tests/fuzz_parse.c
 * @brief Fuzz target for the DSL parser.
 *
 * Uses libFuzzer (built into Clang) to stress-test cdsl_parse_string()
 * with arbitrary input. Detects crashes, hangs, and memory issues.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "cdsl/ast.h"

/**
 * @brief libFuzzer entry point for parser fuzzing.
 *
 * Feeds raw bytes into the DSL parser and frees the result.
 * Returns 0 so the fuzzer continues exploring new inputs.
 */
int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
	/* Copy to a null-terminated buffer */
	char* dsl = malloc(size + 1);
	if (!dsl) {
		return 0;
	}
	memcpy(dsl, data, size);
	dsl[size] = '\0';

	cdsl_error_list_t* errs = NULL;
	cdsl_rule_t* rule = cdsl_parse_string(dsl, &errs);
	if (errs) {
		cdsl_error_list_free(errs);
	}
	if (rule) {
		cdsl_free_rule(rule);
	}
	free(dsl);
	return 0;
}
