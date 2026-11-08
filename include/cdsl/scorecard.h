/**
 * @file scorecard.h
 * @brief Standard Scorecard modeling and explainability engine for credit and risk scoring.
 *
 * Provides baseline scoring, attribute binning (characteristics), weighted delta increments,
 * and detailed per-characteristic contribution reports for full model interpretability.
 */

#ifndef CDSL_SCORECARD_H
#define CDSL_SCORECARD_H

#include "cdsl/util/portability.h"
#include "cdsl/ast.h"
#include "cdsl/context.h"
#include "cdsl/schema.h"
#include "cdsl/util/error.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Individual binning CASE within a characteristic.
 */
typedef struct cdsl_scorecard_case {
	cdsl_expr_node_t* condition;	  /**< Matching condition (e.g. range [18..25] or < 20) */
	double delta;			  /**< Score delta increment or decrement */
	struct cdsl_scorecard_case* next; /**< Next case item */
} cdsl_scorecard_case_t;

/**
 * @brief A characteristic (feature dimension) evaluated by the scorecard.
 */
typedef struct cdsl_scorecard_char {
	char* name;			  /**< Characteristic / variable name */
	cdsl_scorecard_case_t* cases;	  /**< Ordered case conditions */
	double default_delta;		  /**< Fallback delta if no case matches */
	struct cdsl_scorecard_char* next; /**< Next characteristic */
} cdsl_scorecard_char_t;

/**
 * @brief Complete scorecard definition.
 */
typedef struct cdsl_scorecard {
	char* name;				/**< Scorecard name */
	double baseline_score;			/**< Initial baseline score */
	cdsl_scorecard_char_t* characteristics; /**< Linked list of characteristics */
	size_t num_characteristics;		/**< Count of characteristics */
	char* output_var;			/**< Target variable injected into context */
	cdsl_arena_t* arena;			/**< Arena memory if used */
} cdsl_scorecard_t;

/**
 * @brief Individual characteristic contribution record in evaluation report.
 */
typedef struct cdsl_char_contribution {
	char* char_name;      /**< Characteristic name */
	double delta;	      /**< Applied score delta */
	int matched_case_idx; /**< Index of matched case (or -1 for default) */
} cdsl_char_contribution_t;

/**
 * @brief Comprehensive scorecard evaluation report.
 */
typedef struct cdsl_scorecard_report {
	char* scorecard_name;			 /**< Scorecard name */
	double baseline_score;			 /**< Initial baseline score */
	double final_score;			 /**< Total accumulated score */
	cdsl_char_contribution_t* contributions; /**< Per-feature contribution details */
	size_t num_contributions;		 /**< Count of evaluated features */
} cdsl_scorecard_report_t;

/**
 * @brief Create a new scorecard.
 * @param name Scorecard name
 * @param baseline Initial baseline score
 * @param output_var Context variable to receive final score (optional, can be NULL)
 * @return Newly allocated scorecard (free with cdsl_scorecard_free)
 */
cdsl_scorecard_t* cdsl_scorecard_create(const char* name, double baseline, const char* output_var);

/**
 * @brief Add a characteristic dimension to a scorecard.
 * @param sc Scorecard
 * @param name Variable identifier
 * @param default_delta Default score delta if no case matches
 * @return 0 on success, negative on error
 */
int cdsl_scorecard_add_characteristic(cdsl_scorecard_t* sc, const char* name, double default_delta);

/**
 * @brief Add a binning case to a characteristic.
 * @param sc Scorecard
 * @param char_name Target characteristic identifier
 * @param condition Condition expression
 * @param delta Score delta
 * @return 0 on success, negative on error
 */
int cdsl_scorecard_add_case(cdsl_scorecard_t* sc,
			    const char* char_name,
			    cdsl_expr_node_t* condition,
			    double delta);

/**
 * @brief Destroy a scorecard and free all resources.
 * @param sc Scorecard
 */
void cdsl_scorecard_free(cdsl_scorecard_t* sc);

/**
 * @brief Parse a scorecard from DSL text.
 * @param dsl DSL string containing SCORECARD definition
 * @param errors Optional error list container
 * @return Parsed scorecard or NULL on syntax error
 */
cdsl_scorecard_t* cdsl_scorecard_parse_string(const char* dsl, cdsl_error_list_t* errors);

/**
 * @brief Evaluate a scorecard against a runtime context.
 *
 * Accumulates baseline score and characteristic deltas, injecting the final score
 * into ctx if output_var was specified.
 *
 * @param sc Scorecard
 * @param ctx Runtime evaluation context
 * @return Newly allocated report (free with cdsl_scorecard_report_free)
 */
cdsl_scorecard_report_t* cdsl_scorecard_eval(const cdsl_scorecard_t* sc, cdsl_context_t* ctx);

/**
 * @brief Destroy a scorecard evaluation report.
 * @param rpt Report
 */
void cdsl_scorecard_report_free(cdsl_scorecard_report_t* rpt);

/**
 * @brief Verify scorecard against a schema.
 * @param sc Scorecard
 * @param schema Schema
 * @param errors Error list
 * @return 0 on valid, negative on error
 */
int cdsl_schema_verify_scorecard(const cdsl_scorecard_t* sc,
				 const cdsl_schema_t* schema,
				 cdsl_error_list_t* errors);

#ifdef __cplusplus
}
#endif

#endif /* CDSL_SCORECARD_H */
