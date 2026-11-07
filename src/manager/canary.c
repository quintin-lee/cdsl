/**
 * @file src/manager/canary.c
 * @brief Canary Pre-Flight verification and dry-run validation.
 */

#include "cdsl/canary.h"
#include "cdsl/util/portability.h"
#include <stdlib.h>
#include <string.h>

void
cdsl_canary_report_free(cdsl_canary_report_t* report)
{
	if (!report) {
		return;
	}
	if (report->message) {
		free(report->message);
		report->message = NULL;
	}
}

cdsl_canary_report_t
cdsl_canary_verify_dsl(const cdsl_schema_t* schema,
		       const char* dsl_text,
		       cdsl_context_t** mock_contexts,
		       size_t mock_count)
{
	cdsl_canary_report_t rep = {0};

	if (!dsl_text) {
		rep.passed = 0;
		rep.status = CDSL_CANARY_ERR_SYNTAX;
		rep.message = cdsl_strdup("DSL source text is NULL");
		return rep;
	}

	cdsl_error_list_t* parse_err = NULL;
	cdsl_rule_t* rule = cdsl_parse_string(dsl_text, &parse_err);
	if (!rule || (parse_err && cdsl_error_list_has_errors(parse_err))) {
		rep.passed = 0;
		rep.status = CDSL_CANARY_ERR_SYNTAX;
		if (parse_err && parse_err->count > 0 && parse_err->errors[0]) {
			rep.message = cdsl_strdup(parse_err->errors[0]->message);
			rep.line = parse_err->errors[0]->line;
			rep.column = parse_err->errors[0]->column;
		} else {
			rep.message = cdsl_strdup("Syntax error parsing DSL");
		}
		if (rule) {
			cdsl_free_rule(rule);
		}
		cdsl_error_list_free(parse_err);
		return rep;
	}
	if (parse_err) {
		cdsl_error_list_free(parse_err);
	}

	if (schema) {
		cdsl_error_list_t* schema_err = cdsl_verify_rule_detailed(rule, schema);
		if (schema_err && cdsl_error_list_has_errors(schema_err)) {
			rep.passed = 0;
			rep.status = CDSL_CANARY_ERR_SCHEMA;
			if (schema_err->count > 0 && schema_err->errors[0]) {
				rep.message = cdsl_strdup(schema_err->errors[0]->message);
				rep.line = schema_err->errors[0]->line;
				rep.column = schema_err->errors[0]->column;
			} else {
				rep.message = cdsl_strdup("Schema type check failed");
			}
			cdsl_error_list_free(schema_err);
			cdsl_free_rule(rule);
			return rep;
		}
		if (schema_err) {
			cdsl_error_list_free(schema_err);
		}
	}

	if (mock_contexts && mock_count > 0) {
		cdsl_vm_t* dry_vm = cdsl_vm_create(schema);
		if (!dry_vm) {
			rep.passed = 0;
			rep.status = CDSL_CANARY_ERR_DRYRUN;
			rep.message = cdsl_strdup("Failed to initialize canary sandbox VM");
			cdsl_free_rule(rule);
			return rep;
		}

		for (size_t i = 0; i < mock_count; i++) {
			if (!mock_contexts[i]) {
				continue;
			}
			cdsl_rule_report_t* rpt = cdsl_vm_execute(dry_vm, rule, mock_contexts[i]);
			if (!rpt || rpt->status == CDSL_STATUS_ERROR) {
				rep.passed = 0;
				rep.status = CDSL_CANARY_ERR_DRYRUN;
				if (rpt && rpt->error && rpt->error->message) {
					rep.message = cdsl_strdup(rpt->error->message);
				} else {
					rep.message = cdsl_strdup(
					    "Dry-run execution encountered runtime error");
				}
				cdsl_report_free(rpt);
				cdsl_vm_free(dry_vm);
				cdsl_free_rule(rule);
				return rep;
			}
			cdsl_report_free(rpt);
			rep.mocks_executed++;
		}
		cdsl_vm_free(dry_vm);
	}

	cdsl_free_rule(rule);
	rep.passed = 1;
	rep.status = CDSL_CANARY_OK;
	rep.rules_verified = 1;
	rep.message = cdsl_strdup("Canary pre-flight check passed cleanly");
	return rep;
}

int
cdsl_manager_deploy_canary(cdsl_manager_t* mgr,
			   const char* dsl_text,
			   const char* tag,
			   cdsl_context_t** mock_contexts,
			   size_t mock_count,
			   cdsl_canary_report_t* out_report,
			   uint64_t* out_version)
{
	if (!mgr || !dsl_text) {
		return 0;
	}

	cdsl_canary_report_t rep = cdsl_canary_verify_dsl(
	    cdsl_manager_get_schema(mgr), dsl_text, mock_contexts, mock_count);
	if (!rep.passed) {
		if (out_report) {
			*out_report = rep;
		} else {
			cdsl_canary_report_free(&rep);
		}
		return 0;
	}

	int ok = cdsl_manager_deploy_dsl(mgr, dsl_text, tag, out_version, NULL);
	if (out_report) {
		*out_report = rep;
	} else {
		cdsl_canary_report_free(&rep);
	}
	return ok;
}
