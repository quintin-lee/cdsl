/**
 * @file cdsl/codegen.h
 * @brief C code generation from DSL rules.
 *
 * @defgroup cdsl_codegen Code Generation
 * @{
 */
#ifndef CDSL_CODEGEN_H
#define CDSL_CODEGEN_H

#include "cdsl/ast.h"
#include "cdsl/schema.h"
#include "cdsl/ruleset.h"

char* cdsl_codegen_rule_to_c(const cdsl_rule_t* rule, const cdsl_schema_t* schema);
int
cdsl_codegen_to_file(const cdsl_rule_t* rule, const cdsl_schema_t* schema, const char* filepath);

/**
 * @brief Generate C header (.h) content for a ruleset.
 * @param set Ruleset to generate code for
 * @param schema Schema for type and action resolution
 * @param module_name Name of the module (used in function prefixes)
 * @return Newly allocated string (must be freed by caller)
 */
char* cdsl_codegen_ruleset_to_h(const cdsl_ruleset_t* set,
				const cdsl_schema_t* schema,
				const char* module_name);

/**
 * @brief Generate C implementation (.c) content for a ruleset.
 * @param set Ruleset to generate code for
 * @param schema Schema for type and action resolution
 * @param module_name Name of the module (must match header)
 * @return Newly allocated string (must be freed by caller)
 */
char* cdsl_codegen_ruleset_to_c(const cdsl_ruleset_t* set,
				const cdsl_schema_t* schema,
				const char* module_name);

/**
 * @brief Generate both .c and .h files for a ruleset.
 * @param set Ruleset to generate code for
 * @param schema Schema for type and action resolution
 * @param base_path Base path without extension (e.g. "out/my_rules")
 * @return 1 on success, 0 on failure
 */
int cdsl_codegen_ruleset_to_files(const cdsl_ruleset_t* set,
				  const cdsl_schema_t* schema,
				  const char* base_path);

#endif
/** @} */
