/**
 * @file cdsl/schema.h
 * @brief Schema definition and rule verification.
 *
 * @defgroup cdsl_schema Schema
 * @{
 */
#ifndef CDSL_SCHEMA_H
#define CDSL_SCHEMA_H

#include "cdsl/ast.h"
#include "cdsl/util/error.h"
#include "cdsl/util/hashmap.h"

/**
 * @brief Registered variable descriptor.
 */
typedef struct cdsl_var_schema {
	char* name;
	cdsl_type_t type;
	int is_readonly; /**< 1 if variable is read-only at runtime */
	struct cdsl_var_schema* next;
} cdsl_var_schema_t;

/**
 * @brief Registered action descriptor.
 */
typedef struct cdsl_action_schema {
	char* name;
	cdsl_type_t return_type;
	int arg_count;
	cdsl_type_t* arg_types;
	struct cdsl_action_schema* next;
} cdsl_action_schema_t;

/**
 * @brief Schema containing all registered variables and actions.
 */
typedef struct cdsl_schema {
	cdsl_var_schema_t* vars;
	cdsl_action_schema_t* actions;
	cdsl_hashmap_t* var_map;
	cdsl_hashmap_t* action_map;
} cdsl_schema_t;

/**
 * @brief Create an empty schema.
 * @return Newly allocated schema (must be freed with cdsl_schema_free)
 */
[[nodiscard]]
cdsl_schema_t* cdsl_schema_create(void);

/**
 * @brief Free a schema and all its registered entries.
 * @param schema Schema to free (NULL-safe)
 */
void cdsl_schema_free(cdsl_schema_t* schema);

/**
 * @brief Register a variable in the schema.
 * @param schema Target schema
 * @param name Variable name (duplicated internally)
 * @param type Expected data type
 */
void cdsl_schema_register_var(cdsl_schema_t* schema, const char* name, cdsl_type_t type);
/**
 * @brief Register a variable with read-only protection.
 *
 * Read-only variables can be set once but not modified by
 * cdsl_context_set_*() after being bound. Use for configuration
 * variables that rules should not modify during execution.
 *
 * @param schema Target schema
 * @param name Variable name (duplicated internally)
 * @param type Expected data type
 * @param readonly 1 to prevent runtime modification, 0 for normal behavior
 */
void cdsl_schema_register_var_rw(cdsl_schema_t* schema,
				 const char* name,
				 cdsl_type_t type,
				 int readonly);

/**
 * @brief Register an action in the schema.
 * @param schema Target schema
 * @param name Action name (duplicated internally)
 * @param ret_type Return type
 * @param arg_count Number of expected arguments
 * @param ... Variadic list of cdsl_type_t values for each argument type
 */
void cdsl_schema_register_action(
    cdsl_schema_t* schema, const char* name, cdsl_type_t ret_type, int arg_count, ...);

/**
 * @brief Verify a rule against the schema (simple error string).
 * @param rule Parsed AST rule to verify
 * @param schema Registered schema
 * @param[out] err_buf Buffer to receive error message on failure
 * @param err_buf_sz Size of err_buf
 * @return true if rule is valid, false if errors found
 */
bool cdsl_verify_rule(const cdsl_rule_t* rule,
		      const cdsl_schema_t* schema,
		      char* err_buf,
		      int err_buf_sz);

/**
 * @brief Verify a rule with detailed structured error reporting.
 * @param rule Parsed AST rule to verify
 * @param schema Registered schema
 * @return Error list (must be freed with cdsl_error_list_free), or NULL if no errors found
 */
cdsl_error_list_t* cdsl_verify_rule_detailed(const cdsl_rule_t* rule, const cdsl_schema_t* schema);

/**
 * @brief Perform static analysis on a rule for warnings.
 *
 * Detects issues not caught by verification, such as:
 * - Implicit type conversions (e.g., INT → FLOAT promotion)
 * - Unreachable CASE branches (dead code)
 * - Always-true or always-false conditions
 * - Shadowed CASE conditions
 *
 * @param rule Parsed AST rule to analyze
 * @param schema Registered schema
 * @return Warning list (must be freed with cdsl_error_list_free),
 *         NULL if no warnings found. All entries have kind CDSL_ERR_WARNING.
 */
cdsl_error_list_t* cdsl_analyze_rule(const cdsl_rule_t* rule, const cdsl_schema_t* schema);

#endif
/** @} */
