/**
 * @file metrics.h
 * @brief Prometheus metrics collector and exposition format exporter.
 *
 * Provides real-time metrics tracking for rule execution counts, status,
 * evaluation durations (histograms), and memory utilization.
 */

#ifndef CDSL_METRICS_H
#define CDSL_METRICS_H

#include "cdsl/report.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Prometheus metrics collector instance.
 */
typedef struct cdsl_metrics_collector cdsl_metrics_collector_t;

/**
 * @brief Create a new metrics collector.
 * @return Newly allocated collector (free with cdsl_metrics_free)
 */
cdsl_metrics_collector_t* cdsl_metrics_create(void);

/**
 * @brief Free a metrics collector and all allocated counters.
 * @param collector Collector to free (NULL-safe)
 */
void cdsl_metrics_free(cdsl_metrics_collector_t* collector);

/**
 * @brief Reset all counters and histogram observations in the collector.
 * @param collector Target collector
 */
void cdsl_metrics_reset(cdsl_metrics_collector_t* collector);

/**
 * @brief Record a rule evaluation occurrence and duration.
 *
 * Updates counter `cdsl_rule_evaluations_total{rule="...",status="..."}`
 * and observes latency in `cdsl_rule_eval_duration_seconds{rule="..."}`.
 *
 * @param collector Collector
 * @param rule_name Rule name
 * @param status Evaluation outcome status
 * @param duration_seconds Duration in seconds (e.g. 0.00012)
 */
void cdsl_metrics_record_rule(cdsl_metrics_collector_t* collector,
			      const char* rule_name,
			      cdsl_rule_status_t status,
			      double duration_seconds);

/**
 * @brief Increment an arbitrary named counter with a label.
 *
 * @param collector Collector
 * @param metric_name Metric name (e.g. "cdsl_actions_triggered_total")
 * @param label_key Label key (e.g. "action")
 * @param label_val Label value (e.g. "block")
 */
void cdsl_metrics_inc(cdsl_metrics_collector_t* collector,
		      const char* metric_name,
		      const char* label_key,
		      const char* label_val);

/**
 * @brief Set a gauge value.
 *
 * @param collector Collector
 * @param gauge_name Gauge metric name (e.g. "cdsl_active_version")
 * @param value Current gauge value
 */
void
cdsl_metrics_set_gauge(cdsl_metrics_collector_t* collector, const char* gauge_name, double value);

/**
 * @brief Export all collected metrics in standard Prometheus text exposition format (version 0.0.4).
 *
 * @param collector Collector
 * @return Newly malloc'd string containing Prometheus metrics lines (caller must free())
 */
char* cdsl_metrics_export_prometheus(const cdsl_metrics_collector_t* collector);

#ifdef __cplusplus
}
#endif

#endif /* CDSL_METRICS_H */
