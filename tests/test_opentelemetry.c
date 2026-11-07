/**
 * @file tests/test_opentelemetry.c
 * @brief Unit tests for OpenTelemetry decision tracing and W3C trace context.
 */

#include "test.h"
#include "cdsl/cdsl.h"
#include "cdsl/trace.h"
#include <stdlib.h>
#include <string.h>

static void
test_trace_w3c_traceparent_and_attributes(void)
{
	TEST_BEGIN("trace: W3C traceparent formatting and attribute propagation");
	cdsl_tracer_t* tracer = cdsl_tracer_create("cdsl-fraud-engine");
	TEST_ASSERT_NOT_NULL(tracer, "create tracer");

	/* Span 1: Root span */
	cdsl_trace_span_t* root = cdsl_trace_start_span(tracer, "rule_audit", NULL);
	TEST_ASSERT_NOT_NULL(root, "start root span");
	TEST_ASSERT_INT((int)strlen(root->trace_id), 32, "trace_id has 32 hex chars");
	TEST_ASSERT_INT((int)strlen(root->span_id), 16, "span_id has 16 hex chars");
	TEST_ASSERT_INT((int)strlen(root->parent_span_id), 0, "parent_span_id is empty");

	cdsl_trace_set_attr_string(root, "rule.name", "fraud_check");
	cdsl_trace_set_attr_int(root, "rule.risk_score", 95);
	cdsl_trace_set_attr_double(root, "rule.threshold", 80.0);
	cdsl_trace_end_span(root, CDSL_SPAN_OK, "Check completed");

	/* Get traceparent header */
	char traceparent[64];
	int ok = cdsl_trace_get_traceparent(root, traceparent, sizeof(traceparent));
	TEST_ASSERT_INT(ok, 1, "format traceparent");
	TEST_ASSERT(strncmp(traceparent, "00-", 3) == 0, "version is 00");

	/* Span 2: Child span inheriting traceparent */
	cdsl_trace_span_t* child = cdsl_trace_start_span(tracer, "action_block", traceparent);
	TEST_ASSERT_NOT_NULL(child, "start child span");
	TEST_ASSERT_STR(child->trace_id, root->trace_id, "child has same trace_id as root");
	TEST_ASSERT_STR(child->parent_span_id, root->span_id, "child parent_span_id matches root");

	cdsl_trace_end_span(child, CDSL_SPAN_OK, NULL);

	/* Export OTLP JSON */
	char* json = cdsl_trace_export_json(tracer);
	TEST_ASSERT_NOT_NULL(json, "export JSON");
	TEST_ASSERT(strstr(json, "\"cdsl-fraud-engine\"") != NULL, "service name present in JSON");
	TEST_ASSERT(strstr(json, "\"rule_audit\"") != NULL, "span name present");
	TEST_ASSERT(strstr(json, "\"action_block\"") != NULL, "child span name present");
	TEST_ASSERT(strstr(json, "\"rule.risk_score\"") != NULL, "attributes present");

	free(json);
	cdsl_tracer_free(tracer);
	TEST_END();
}

int
main(void)
{
	printf("Running OpenTelemetry Decision Tracing tests...\n");
	test_trace_w3c_traceparent_and_attributes();
	TEST_SUMMARY();
	TEST_EXIT();
}
