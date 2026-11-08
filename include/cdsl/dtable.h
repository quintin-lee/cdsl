/**
 * @file dtable.h
 * @brief Enterprise Decision Table (DMN-style) modeling and evaluation engine.
 *
 * Supports multi-dimensional condition matching, multiple hit policies
 * (FIRST, RULE_ORDER, UNIQUE), wildcard matching, and structured decision reports.
 */

#ifndef CDSL_DTABLE_H
#define CDSL_DTABLE_H

#include "cdsl/util/portability.h"
#include "cdsl/ast.h"
#include "cdsl/context.h"
#include "cdsl/schema.h"
#include "cdsl/util/error.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Hit policies defining decision table execution semantics.
 */
typedef enum {
	CDSL_HIT_FIRST = 0,	 /**< First matching row terminates evaluation */
	CDSL_HIT_RULE_ORDER = 1, /**< All matching rows collected in order */
	CDSL_HIT_UNIQUE = 2	 /**< Exactly one row must match; >1 is an error */
} cdsl_hit_policy_t;

/**
 * @brief Discriminator for decision table cell types.
 */
typedef enum {
	CDSL_CELL_WILDCARD = 0, /**< Wildcard '_' (matches any value) */
	CDSL_CELL_EXPR = 1	/**< Expression condition or output value */
} cdsl_cell_kind_t;

/**
 * @brief Individual cell in a decision table row.
 */
typedef struct cdsl_dtable_cell {
	cdsl_cell_kind_t kind;	/**< Cell kind (wildcard or expression) */
	cdsl_expr_node_t* expr; /**< Expression node (or NULL if wildcard) */
} cdsl_dtable_cell_t;

/**
 * @brief Column definition for decision table inputs or outputs.
 */
typedef struct cdsl_dtable_col {
	char* name;	  /**< Identifier name */
	cdsl_type_t type; /**< Declared data type */
} cdsl_dtable_col_t;

/**
 * @brief A single decision rule row in a decision table.
 */
typedef struct cdsl_dtable_row {
	size_t row_id;		      /**< 1-based row index */
	cdsl_dtable_cell_t* inputs;   /**< Input condition cells [num_inputs] */
	cdsl_dtable_cell_t* outputs;  /**< Output action cells [num_outputs] */
	struct cdsl_dtable_row* next; /**< Next row in table */
} cdsl_dtable_row_t;

/**
 * @brief Decision table AST structure.
 */
typedef struct cdsl_dtable {
	char* name;			     /**< Table identifier */
	cdsl_hit_policy_t hit_policy;	     /**< Hit policy */
	cdsl_dtable_col_t* input_cols;	     /**< Input column metadata array */
	size_t num_inputs;		     /**< Count of input columns */
	cdsl_dtable_col_t* output_cols;	     /**< Output column metadata array */
	size_t num_outputs;		     /**< Count of output columns */
	cdsl_dtable_row_t* rows;	     /**< Linked list of rows */
	size_t num_rows;		     /**< Count of rows */
	cdsl_dtable_cell_t* default_outputs; /**< Fallback output cells [num_outputs] */
	cdsl_arena_t* arena;		     /**< Memory arena if used */
} cdsl_dtable_t;

/**
 * @brief Recorded match entry in evaluation report.
 */
typedef struct cdsl_dtable_match {
	size_t row_id;	       /**< Matched row ID */
	cdsl_value_t* outputs; /**< Evaluated output values [num_outputs] */
} cdsl_dtable_match_t;

/**
 * @brief Evaluation result report for a decision table.
 */
typedef struct cdsl_dtable_report {
	char* table_name;	      /**< Table name */
	bool matched;		      /**< True if at least one row or default matched */
	cdsl_dtable_match_t* matches; /**< Array of matching row records */
	size_t num_matches;	      /**< Count of matching rows */
	cdsl_value_t* final_outputs;  /**< Primary output values [num_outputs] */
	size_t num_outputs;	      /**< Count of outputs */
	char* error_message;	      /**< Error details (e.g. for UNIQUE violation) */
} cdsl_dtable_report_t;

/**
 * @brief Create a new decision table.
 * @param name Unique table name
 * @param hit_policy Hit policy
 * @return Newly allocated table (free with cdsl_dtable_free)
 */
cdsl_dtable_t* cdsl_dtable_create(const char* name, cdsl_hit_policy_t hit_policy);

/**
 * @brief Add an input column definition.
 * @param dt Table
 * @param name Variable identifier
 * @param type Data type
 * @return 0 on success, negative on error
 */
int cdsl_dtable_add_input(cdsl_dtable_t* dt, const char* name, cdsl_type_t type);

/**
 * @brief Add an output column definition.
 * @param dt Table
 * @param name Variable identifier
 * @param type Data type
 * @return 0 on success, negative on error
 */
int cdsl_dtable_add_output(cdsl_dtable_t* dt, const char* name, cdsl_type_t type);

/**
 * @brief Add a row to the decision table.
 * @param dt Table
 * @param inputs Array of cells of length dt->num_inputs
 * @param outputs Array of cells of length dt->num_outputs
 * @return 0 on success, negative on error
 */
int cdsl_dtable_add_row(cdsl_dtable_t* dt,
			const cdsl_dtable_cell_t* inputs,
			const cdsl_dtable_cell_t* outputs);

/**
 * @brief Set default outputs if no row matches.
 * @param dt Table
 * @param default_outputs Array of cells of length dt->num_outputs
 * @return 0 on success, negative on error
 */
int cdsl_dtable_set_default(cdsl_dtable_t* dt, const cdsl_dtable_cell_t* default_outputs);

/**
 * @brief Destroy a decision table and free all its resources.
 * @param dt Table
 */
void cdsl_dtable_free(cdsl_dtable_t* dt);

/**
 * @brief Parse a decision table from DSL text.
 * @param dsl DSL string containing DECISION_TABLE definition
 * @param errors Optional error list container
 * @return Parsed table or NULL on syntax error
 */
cdsl_dtable_t* cdsl_dtable_parse_string(const char* dsl, cdsl_error_list_t* errors);

/**
 * @brief Evaluate a decision table against a runtime context.
 * @param dt Decision table
 * @param ctx Context containing input variables
 * @return Newly allocated report (free with cdsl_dtable_report_free)
 */
cdsl_dtable_report_t* cdsl_dtable_eval(const cdsl_dtable_t* dt, cdsl_context_t* ctx);

/**
 * @brief Destroy a decision table evaluation report.
 * @param rpt Report
 */
void cdsl_dtable_report_free(cdsl_dtable_report_t* rpt);

/**
 * @brief Verify decision table against a schema.
 * @param dt Decision table
 * @param schema Schema
 * @param errors Error list
 * @return 0 on valid, negative on error
 */
int cdsl_schema_verify_dtable(const cdsl_dtable_t* dt,
			      const cdsl_schema_t* schema,
			      cdsl_error_list_t* errors);

#ifdef __cplusplus
}
#endif

#endif /* CDSL_DTABLE_H */
