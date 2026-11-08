/**
 * @file wasm/cdsl_wasm.c
 * @brief WebAssembly & WASI C bridge for CDSL runtime.
 */

#include "cdsl/cdsl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#define CDSL_WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define CDSL_WASM_EXPORT
#endif

CDSL_WASM_EXPORT
void
cdsl_wasm_free(char* ptr)
{
	if (ptr) {
		free(ptr);
	}
}

CDSL_WASM_EXPORT
char*
cdsl_wasm_eval_rule(const char* rule_dsl, const char* json_ctx)
{
	if (!rule_dsl) {
		return cdsl_strdup("{\"error\": \"null rule DSL\"}");
	}

	cdsl_context_t* ctx = cdsl_context_create(NULL);
	if (json_ctx && strlen(json_ctx) > 0) {
		cdsl_context_load_json(ctx, json_ctx);
	}

	cdsl_rule_t* rule = cdsl_parse_string(rule_dsl, NULL);
	if (!rule) {
		cdsl_context_free(ctx);
		return cdsl_strdup("{\"error\": \"failed to parse rule\"}");
	}

	cdsl_vm_t* vm = cdsl_vm_create(NULL);
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);

	char* json_res = NULL;
	if (rpt) {
		json_res = cdsl_report_to_json(rpt);
		cdsl_report_free(rpt);
	} else {
		json_res = cdsl_strdup("{\"error\": \"rule execution returned null\"}");
	}

	cdsl_vm_free(vm);
	cdsl_free_rule(rule);
	cdsl_context_free(ctx);

	return json_res;
}

CDSL_WASM_EXPORT
char*
cdsl_wasm_eval_dtable(const char* dt_dsl, const char* json_ctx)
{
	if (!dt_dsl) {
		return cdsl_strdup("{\"error\": \"null decision table DSL\"}");
	}

	cdsl_context_t* ctx = cdsl_context_create(NULL);
	if (json_ctx && strlen(json_ctx) > 0) {
		cdsl_context_load_json(ctx, json_ctx);
	}

	cdsl_dtable_t* dt = cdsl_dtable_parse_string(dt_dsl, NULL);
	if (!dt) {
		cdsl_context_free(ctx);
		return cdsl_strdup("{\"error\": \"failed to parse decision table\"}");
	}

	cdsl_dtable_report_t* rpt = cdsl_dtable_eval(dt, ctx);
	char buf[512];
	if (rpt) {
		snprintf(buf,
			 sizeof(buf),
			 "{\"table_name\": \"%s\", \"matched\": %s, \"num_matches\": %zu}",
			 rpt->table_name ? rpt->table_name : "",
			 rpt->matched ? "true" : "false",
			 rpt->num_matches);
		cdsl_dtable_report_free(rpt);
	} else {
		snprintf(buf, sizeof(buf), "{\"error\": \"evaluation failed\"}");
	}

	cdsl_dtable_free(dt);
	cdsl_context_free(ctx);

	return cdsl_strdup(buf);
}

CDSL_WASM_EXPORT
char*
cdsl_wasm_eval_scorecard(const char* sc_dsl, const char* json_ctx)
{
	if (!sc_dsl) {
		return cdsl_strdup("{\"error\": \"null scorecard DSL\"}");
	}

	cdsl_context_t* ctx = cdsl_context_create(NULL);
	if (json_ctx && strlen(json_ctx) > 0) {
		cdsl_context_load_json(ctx, json_ctx);
	}

	cdsl_scorecard_t* sc = cdsl_scorecard_parse_string(sc_dsl, NULL);
	if (!sc) {
		cdsl_context_free(ctx);
		return cdsl_strdup("{\"error\": \"failed to parse scorecard\"}");
	}

	cdsl_scorecard_report_t* rpt = cdsl_scorecard_eval(sc, ctx);
	char buf[512];
	if (rpt) {
		snprintf(
		    buf,
		    sizeof(buf),
		    "{\"scorecard_name\": \"%s\", \"baseline_score\": %.2f, \"final_score\": %.2f}",
		    rpt->scorecard_name ? rpt->scorecard_name : "",
		    rpt->baseline_score,
		    rpt->final_score);
		cdsl_scorecard_report_free(rpt);
	} else {
		snprintf(buf, sizeof(buf), "{\"error\": \"scorecard evaluation failed\"}");
	}

	cdsl_scorecard_free(sc);
	cdsl_context_free(ctx);

	return cdsl_strdup(buf);
}

CDSL_WASM_EXPORT
char*
cdsl_wasm_eval_pipeline(const char* pipe_dsl, const char* json_ctx)
{
	if (!pipe_dsl) {
		return cdsl_strdup("{\"error\": \"null pipeline DSL\"}");
	}

	cdsl_context_t* ctx = cdsl_context_create(NULL);
	if (json_ctx && strlen(json_ctx) > 0) {
		cdsl_context_load_json(ctx, json_ctx);
	}

	cdsl_pipeline_t* p = cdsl_pipeline_parse_string(pipe_dsl, NULL);
	if (!p) {
		cdsl_context_free(ctx);
		return cdsl_strdup("{\"error\": \"failed to parse pipeline\"}");
	}

	cdsl_vm_t* vm = cdsl_vm_create(NULL);
	cdsl_pipeline_report_t* rpt = cdsl_pipeline_execute(p, vm, ctx);
	char buf[512];
	if (rpt) {
		snprintf(buf,
			 sizeof(buf),
			 "{\"pipeline_name\": \"%s\", \"status\": %d, \"num_stages\": %zu, "
			 "\"duration_us\": %.2f}",
			 rpt->pipeline_name ? rpt->pipeline_name : "",
			 rpt->status,
			 rpt->num_stages,
			 rpt->total_duration_us);
		cdsl_pipeline_report_free(rpt);
	} else {
		snprintf(buf, sizeof(buf), "{\"error\": \"pipeline execution failed\"}");
	}

	cdsl_vm_free(vm);
	cdsl_pipeline_free(p);
	cdsl_context_free(ctx);

	return cdsl_strdup(buf);
}
