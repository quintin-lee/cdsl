/**
 * @file pipeline.h
 * @brief DAG Rule Pipeline Orchestration Engine.
 *
 * Coordinates multi-stage rule execution with dependency checking, shared context
 * state propagation, and early-abort failure short-circuiting.
 */

#ifndef CDSL_PIPELINE_H
#define CDSL_PIPELINE_H

#include "cdsl/util/portability.h"
#include "cdsl/ast.h"
#include "cdsl/context.h"
#include "cdsl/vm.h"
#include "cdsl/dtable.h"
#include "cdsl/scorecard.h"
#include "cdsl/util/error.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Discriminator for items executable within a stage.
 */
typedef enum {
	CDSL_STAGE_ITEM_RULE = 0,     /**< Standard C-DSL Rule */
	CDSL_STAGE_ITEM_DTABLE = 1,   /**< Decision Table */
	CDSL_STAGE_ITEM_SCORECARD = 2 /**< Scorecard */
} cdsl_stage_item_kind_t;

/**
 * @brief Action taken when a stage fails.
 */
typedef enum {
	CDSL_ON_FAIL_ABORT = 0,	  /**< Abort pipeline immediately */
	CDSL_ON_FAIL_CONTINUE = 1 /**< Record failure and proceed to next stage */
} cdsl_on_fail_t;

/**
 * @brief Execution item within a pipeline stage.
 */
typedef struct cdsl_stage_item {
	cdsl_stage_item_kind_t kind;  /**< Item discriminator */
	char* name;		      /**< Item name / identifier */
	cdsl_rule_t* rule;	      /**< Rule pointer (if kind == RULE) */
	cdsl_dtable_t* dtable;	      /**< Decision table pointer (if kind == DTABLE) */
	cdsl_scorecard_t* scorecard;  /**< Scorecard pointer (if kind == SCORECARD) */
	struct cdsl_stage_item* next; /**< Next item in stage */
} cdsl_stage_item_t;

/**
 * @brief A single execution stage in the pipeline DAG.
 */
typedef struct cdsl_pipeline_stage {
	char* name;			  /**< Unique stage identifier */
	cdsl_stage_item_t* items;	  /**< Linked list of items */
	size_t num_items;		  /**< Count of items */
	cdsl_on_fail_t on_fail;		  /**< Failure policy */
	char** dependencies;		  /**< Array of stage names this stage depends on */
	size_t num_dependencies;	  /**< Count of dependencies */
	struct cdsl_pipeline_stage* next; /**< Next stage in pipeline */
} cdsl_pipeline_stage_t;

/**
 * @brief Complete pipeline definition.
 */
typedef struct cdsl_pipeline {
	char* name;		       /**< Pipeline name */
	cdsl_pipeline_stage_t* stages; /**< Linked list of stages */
	size_t num_stages;	       /**< Count of stages */
	cdsl_arena_t* arena;	       /**< Arena memory if used */
} cdsl_pipeline_t;

/**
 * @brief Pipeline overall execution status.
 */
typedef enum {
	CDSL_PIPELINE_SUCCESS = 0, /**< All stages passed successfully */
	CDSL_PIPELINE_ABORTED = 1, /**< Short-circuited early due to ON_FAIL: ABORT */
	CDSL_PIPELINE_FAILED = 2,  /**< Completed with one or more non-fatal stage failures */
	CDSL_PIPELINE_ERROR = 3	   /**< System or runtime evaluation error */
} cdsl_pipeline_status_t;

/**
 * @brief Execution report for a single stage.
 */
typedef struct cdsl_stage_report {
	char* stage_name;	   /**< Stage identifier */
	bool passed;		   /**< True if all items in stage passed */
	double duration_us;	   /**< Stage execution time in microseconds */
	size_t num_items_executed; /**< Items evaluated in stage */
} cdsl_stage_report_t;

/**
 * @brief Complete pipeline execution report.
 */
typedef struct cdsl_pipeline_report {
	char* pipeline_name;		    /**< Pipeline name */
	cdsl_pipeline_status_t status;	    /**< Overall pipeline status */
	cdsl_stage_report_t* stage_reports; /**< Stage-by-stage results array */
	size_t num_stages;		    /**< Count of evaluated stages */
	double total_duration_us;	    /**< Total pipeline execution duration */
} cdsl_pipeline_report_t;

/**
 * @brief Create a new pipeline.
 * @param name Pipeline name
 * @return Newly allocated pipeline (free with cdsl_pipeline_free)
 */
cdsl_pipeline_t* cdsl_pipeline_create(const char* name);

/**
 * @brief Create an unattached pipeline stage.
 * @param name Unique stage name
 * @param on_fail Failure policy
 * @return Newly allocated stage
 */
cdsl_pipeline_stage_t* cdsl_pipeline_create_stage(const char* name, cdsl_on_fail_t on_fail);

/**
 * @brief Add a rule item to a stage.
 * @param stage Target stage
 * @param rule Rule to evaluate
 * @return 0 on success, negative on error
 */
int cdsl_pipeline_stage_add_rule(cdsl_pipeline_stage_t* stage, cdsl_rule_t* rule);

/**
 * @brief Add a decision table item to a stage.
 * @param stage Target stage
 * @param dt Decision table to evaluate
 * @return 0 on success, negative on error
 */
int cdsl_pipeline_stage_add_dtable(cdsl_pipeline_stage_t* stage, cdsl_dtable_t* dt);

/**
 * @brief Add a scorecard item to a stage.
 * @param stage Target stage
 * @param sc Scorecard to evaluate
 * @return 0 on success, negative on error
 */
int cdsl_pipeline_stage_add_scorecard(cdsl_pipeline_stage_t* stage, cdsl_scorecard_t* sc);

/**
 * @brief Add a named stage dependency to a stage.
 * @param stage Target stage
 * @param dep_stage_name Name of prerequisite stage
 * @return 0 on success, negative on error
 */
int cdsl_pipeline_stage_add_dependency(cdsl_pipeline_stage_t* stage, const char* dep_stage_name);

/**
 * @brief Attach a stage to the pipeline.
 * @param p Pipeline
 * @param stage Stage to attach (pipeline assumes ownership)
 * @return 0 on success, negative on error
 */
int cdsl_pipeline_add_stage(cdsl_pipeline_t* p, cdsl_pipeline_stage_t* stage);

/**
 * @brief Destroy a pipeline and free all its resources.
 * @param p Pipeline
 */
void cdsl_pipeline_free(cdsl_pipeline_t* p);

/**
 * @brief Parse a pipeline from DSL text.
 * @param dsl DSL string containing PIPELINE definition
 * @param errors Optional error list container
 * @return Parsed pipeline or NULL on syntax error
 */
cdsl_pipeline_t* cdsl_pipeline_parse_string(const char* dsl, cdsl_error_list_t* errors);

/**
 * @brief Execute a pipeline against a runtime context using a VM.
 *
 * Evaluates stages in dependency order. Shared context mutations from each stage
 * are immediately visible to subsequent stages.
 *
 * @param p Pipeline
 * @param vm Virtual Machine instance for rule evaluation
 * @param ctx Context containing data and receiving outputs
 * @return Newly allocated pipeline report (free with cdsl_pipeline_report_free)
 */
cdsl_pipeline_report_t*
cdsl_pipeline_execute(cdsl_pipeline_t* p, cdsl_vm_t* vm, cdsl_context_t* ctx);

/**
 * @brief Destroy a pipeline report.
 * @param rpt Report
 */
void cdsl_pipeline_report_free(cdsl_pipeline_report_t* rpt);

#ifdef __cplusplus
}
#endif

#endif /* CDSL_PIPELINE_H */
