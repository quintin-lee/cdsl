/**
 * @file canary.h
 * @brief Canary Pre-Flight verification and dry-run validation.
 *
 * Prevents faulty rule definitions from ever being deployed to production
 * by executing syntax validation, static type checking, and isolated dry-run
 * smoke tests against mock contexts before accepting hot-reload updates.
 */

#ifndef CDSL_CANARY_H
#define CDSL_CANARY_H

#include "cdsl/schema.h"
#include "cdsl/context.h"
#include "cdsl/manager.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Canary Pre-Flight check result status.
 */
typedef enum {
	CDSL_CANARY_OK = 0,	/**< Pre-flight check passed cleanly */
	CDSL_CANARY_ERR_SYNTAX, /**< DSL syntax or parsing error */
	CDSL_CANARY_ERR_SCHEMA, /**< Type or schema verification error */
	CDSL_CANARY_ERR_DRYRUN	/**< Runtime error during mock context execution */
} cdsl_canary_status_t;

/**
 * @brief Detailed report from Canary Pre-Flight verification.
 */
typedef struct cdsl_canary_report {
	int passed;		     /**< 1 if check passed, 0 if failed */
	cdsl_canary_status_t status; /**< Error categorization */
	char* message;		     /**< Diagnostic description */
	int line;		     /**< Source error line (0 if not applicable) */
	int column;		     /**< Source error column (0 if not applicable) */
	size_t rules_verified;	     /**< Number of valid rules parsed */
	size_t mocks_executed;	     /**< Number of mock contexts successfully evaluated */
} cdsl_canary_report_t;

/**
 * @brief Perform a Canary Pre-Flight check on raw DSL text.
 *
 * 1. Parses DSL text into AST rules.
 * 2. Statically verifies all rules against the schema.
 * 3. Runs an isolated dry-run execution against all provided mock contexts.
 *
 * @param schema Schema for static type checking
 * @param dsl_text DSL rule source text
 * @param mock_contexts Array of mock execution contexts for dry-run validation (optional)
 * @param mock_count Number of mock contexts
 * @return Canary report struct (free dynamically allocated message with cdsl_canary_report_free)
 */
cdsl_canary_report_t cdsl_canary_verify_dsl(const cdsl_schema_t* schema,
					    const char* dsl_text,
					    cdsl_context_t** mock_contexts,
					    size_t mock_count);

/**
 * @brief Free dynamically allocated fields inside a canary report.
 * @param report Report to clean up
 */
void cdsl_canary_report_free(cdsl_canary_report_t* report);

/**
 * @brief Deploy a new DSL rule version through Canary Pre-Flight validation.
 *
 * If pre-flight check fails, the deployment is rejected, the active ruleset is untouched,
 * and out_report contains full diagnostics.
 *
 * @param mgr Rule manager
 * @param dsl_text DSL source text
 * @param tag Optional tag
 * @param mock_contexts Mock contexts for dry-run validation
 * @param mock_count Number of mock contexts
 * @param out_report Pointer to receive pre-flight report (optional)
 * @param out_version Pointer to receive assigned version ID (optional)
 * @return 1 if deployed successfully, 0 if rejected
 */
int cdsl_manager_deploy_canary(cdsl_manager_t* mgr,
			       const char* dsl_text,
			       const char* tag,
			       cdsl_context_t** mock_contexts,
			       size_t mock_count,
			       cdsl_canary_report_t* out_report,
			       uint64_t* out_version);

#ifdef __cplusplus
}
#endif

#endif /* CDSL_CANARY_H */
