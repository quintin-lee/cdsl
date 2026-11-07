/**
 * @file tests/test_metrics.c
 * @brief Unit tests for Prometheus metrics collection and exposition format.
 */

#include "test.h"
#include "cdsl/cdsl.h"
#include "cdsl/metrics.h"
#include <stdlib.h>
#include <string.h>

static void
test_metrics_basic_recording_and_export(void)
{
	TEST_BEGIN("metrics: record rule evaluations and export Prometheus format");
	cdsl_metrics_collector_t* c = cdsl_metrics_create();
	TEST_ASSERT_NOT_NULL(c, "create metrics collector");

	cdsl_metrics_record_rule(c, "auth_check", CDSL_STATUS_PASSED, 0.0002);
	cdsl_metrics_record_rule(c, "auth_check", CDSL_STATUS_PASSED, 0.0004);
	cdsl_metrics_record_rule(c, "auth_check", CDSL_STATUS_FAILED, 0.0015);
	cdsl_metrics_record_rule(c, "risk_eval", CDSL_STATUS_FAILED, 0.0050);

	cdsl_metrics_set_gauge(c, "cdsl_active_version", 3.0);
	cdsl_metrics_inc(c, "cdsl_actions_triggered_total", "action", "block");

	char* prom = cdsl_metrics_export_prometheus(c);
	TEST_ASSERT_NOT_NULL(prom, "export prometheus metrics");

	TEST_ASSERT(
	    strstr(prom, "cdsl_rule_evaluations_total{rule=\"auth_check\",status=\"passed\"} 2") !=
		NULL,
	    "auth_check passed count is 2");
	TEST_ASSERT(
	    strstr(prom, "cdsl_rule_evaluations_total{rule=\"auth_check\",status=\"failed\"} 1") !=
		NULL,
	    "auth_check failed count is 1");
	TEST_ASSERT(
	    strstr(prom,
		   "cdsl_rule_eval_duration_seconds_bucket{rule=\"auth_check\",le=\"0.0005\"} 2") !=
		NULL,
	    "bucket 0.0005 count is 2");
	TEST_ASSERT(strstr(prom, "cdsl_active_version 3.0000") != NULL, "gauge exported");
	TEST_ASSERT(strstr(prom, "cdsl_actions_triggered_total{action=\"block\"} 1") != NULL,
		    "custom counter exported");

	free(prom);
	cdsl_metrics_free(c);
	TEST_END();
}

static void
test_metrics_reset(void)
{
	TEST_BEGIN("metrics: reset counters and gauges");
	cdsl_metrics_collector_t* c = cdsl_metrics_create();
	cdsl_metrics_record_rule(c, "test_rule", CDSL_STATUS_PASSED, 0.001);
	cdsl_metrics_set_gauge(c, "my_gauge", 10.0);

	cdsl_metrics_reset(c);

	char* prom = cdsl_metrics_export_prometheus(c);
	TEST_ASSERT_NOT_NULL(prom, "export empty prometheus");
	TEST_ASSERT(strstr(prom, "test_rule") == NULL, "rule cleared after reset");
	TEST_ASSERT(strstr(prom, "my_gauge") == NULL, "gauge cleared after reset");

	free(prom);
	cdsl_metrics_free(c);
	TEST_END();
}

int
main(void)
{
	printf("Running Prometheus Metrics tests...\n");
	test_metrics_basic_recording_and_export();
	test_metrics_reset();
	TEST_SUMMARY();
	TEST_EXIT();
}
