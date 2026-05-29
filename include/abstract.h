/**
 * @file abstract.h
 * @brief Abstract layer: schema definition and rule verification.
 *
 * Provides the type system and schema registration API used for static
 * analysis of DSL rules before execution. Catches type mismatches,
 * unknown variables, and action signature errors at verification time.
 *
 * @defgroup abstract Abstract Layer
 * @{
 */

#ifndef CDSL_ABSTRACT_H
#define CDSL_ABSTRACT_H

#include "ast.h"
#include "cdsl_error.h"

/**
 * @brief Registered variable descriptor.
 *
 * Each variable registered in the schema has a name (dot-separated path
 * like "user.age") and a type. The verification engine uses these to
 * check that all variables referenced in rules exist and have compatible types.
 */
typedef struct cdsl_var_schema {
	char* name;		      /**< Variable name (e.g. "user.age") */
	cdsl_type_t type;	      /**< Variable data type */
	struct cdsl_var_schema* next; /**< Next variable in linked list */
} cdsl_var_schema_t;

/**
 * @brief Registered action descriptor.
 *
 * Actions are callbacks triggered when rule conditions are met.
 * The schema stores the expected signature so the verifier can check
 * argument count and types at verification time.
 */
typedef struct cdsl_action_schema {
	char* name;			 /**< Action name (e.g. "reject_supplier") */
	cdsl_type_t return_type;	 /**< Return type (typically VOID) */
	int arg_count;			 /**< Expected number of arguments */
	cdsl_type_t* arg_types;		 /**< Array of expected argument types */
	struct cdsl_action_schema* next; /**< Next action in linked list */
} cdsl_action_schema_t;

/**
 * @brief Schema containing all registered variables and actions.
 *
 * The schema acts as a contract between the DSL rules and the host
 * application. Register all variables and actions before parsing rules.
 *
 * @code
 * cdsl_schema_t* schema = cdsl_schema_create();
 * cdsl_schema_register_var(schema, "user.age", CDSL_TYPE_INT);
 * cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
 * @endcode
 */
typedef struct cdsl_schema {
	cdsl_var_schema_t* vars;       /**< Registered variables */
	cdsl_action_schema_t* actions; /**< Registered actions */
} cdsl_schema_t;

/**
 * @brief Create an empty schema.
 * @return Newly allocated schema (must be freed with cdsl_schema_free)
 */
cdsl_schema_t* cdsl_schema_create(void);

/**
 * @brief Free a schema and all its registered entries.
 * @param schema Schema to free (NULL-safe)
 */
void cdsl_schema_free(cdsl_schema_t* schema);

/**
 * @brief Register a variable in the schema.
 *
 * @param schema Target schema
 * @param name Variable name (duplicated internally), e.g. "user.age"
 * @param type Expected data type
 */
void cdsl_schema_register_var(cdsl_schema_t* schema, const char* name, cdsl_type_t type);

/**
 * @brief Register an action in the schema.
 *
 * @param schema Target schema
 * @param name Action name (duplicated internally)
 * @param return_type Return type (as int, enum promoted to int in variadic context)
 * @param arg_count Number of expected arguments
 * @param ... Variadic list of int values for each argument type
 */
void cdsl_schema_register_action(
    cdsl_schema_t* schema, const char* name, int ret_type, int arg_count, ...);

/**
 * @brief Verify a rule against the schema (simple error string).
 *
 * @param rule Parsed AST rule
 * @param schema Registered schema
 * @param err_buf Buffer to receive error message on failure
 * @param err_buf_sz Size of error buffer
 * @return 1 if valid, 0 if errors found
 */
int cdsl_verify_rule(const cdsl_rule_t* rule,
		     const cdsl_schema_t* schema,
		     char* err_buf,
		     int err_buf_sz);

/**
 * @brief Verify a rule with detailed structured error reporting.
 *
 * Unlike cdsl_verify_rule(), this function collects ALL errors instead
 * of stopping at the first one. Useful for IDE-like diagnostics.
 *
 * @param rule Parsed AST rule
 * @param schema Registered schema
 * @return Error list (must be freed with cdsl_error_list_free)
 */
cdsl_error_list_t* cdsl_verify_rule_detailed(const cdsl_rule_t* rule, const cdsl_schema_t* schema);

#endif
/** @} */
