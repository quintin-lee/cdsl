/**
 * @file trace.h
 * @brief OpenTelemetry decision tracing and W3C distributed trace context.
 *
 * Captures fine-grained audit spans for rule evaluations, metric calculations,
 * and decision trees, enabling end-to-end regulatory compliance and distributed
 * transaction observability.
 */

#ifndef CDSL_TRACE_H
#define CDSL_TRACE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CDSL_TRACE_ID_LEN 32
#define CDSL_SPAN_ID_LEN 16

/**
 * @brief OpenTelemetry Span status code.
 */
typedef enum { CDSL_SPAN_UNSET = 0, CDSL_SPAN_OK = 1, CDSL_SPAN_ERROR = 2 } cdsl_span_status_t;

/**
 * @brief Key-value attribute in an OpenTelemetry span.
 */
typedef struct cdsl_span_attr {
	char* key;
	char* string_val;
	int64_t int_val;
	double double_val;
	int val_type; /* 0: string, 1: int, 2: double */
	struct cdsl_span_attr* next;
} cdsl_span_attr_t;

/**
 * @brief OpenTelemetry decision span.
 */
typedef struct cdsl_trace_span {
	char trace_id[CDSL_TRACE_ID_LEN + 1];
	char span_id[CDSL_SPAN_ID_LEN + 1];
	char parent_span_id[CDSL_SPAN_ID_LEN + 1];
	char* name;
	uint64_t start_time_unix_nano;
	uint64_t end_time_unix_nano;
	cdsl_span_status_t status;
	char* status_description;
	cdsl_span_attr_t* attributes;
	struct cdsl_trace_span* next;
} cdsl_trace_span_t;

/**
 * @brief OpenTelemetry tracer handle.
 */
typedef struct cdsl_tracer cdsl_tracer_t;

/**
 * @brief Create a new OpenTelemetry decision tracer.
 * @param service_name Logical service identifier (e.g. "cdsl-risk-engine")
 * @return Newly allocated tracer (free with cdsl_tracer_free)
 */
cdsl_tracer_t* cdsl_tracer_create(const char* service_name);

/**
 * @brief Free an OpenTelemetry tracer and all accumulated spans.
 * @param tracer Tracer to free (NULL-safe)
 */
void cdsl_tracer_free(cdsl_tracer_t* tracer);

/**
 * @brief Clear all spans currently held by the tracer.
 * @param tracer Tracer
 */
void cdsl_tracer_clear(cdsl_tracer_t* tracer);

/**
 * @brief Start a new decision span within the trace.
 *
 * If @p parent_traceparent is provided (W3C format "00-<trace_id>-<span_id>-01"),
 * the span adopts that trace ID and parent span ID. Otherwise a new random trace ID is generated.
 *
 * @param tracer Tracer
 * @param name Span name (e.g. "rule_decision", "metric_evaluation")
 * @param parent_traceparent W3C traceparent header string (optional, may be NULL)
 * @return Newly started span
 */
cdsl_trace_span_t*
cdsl_trace_start_span(cdsl_tracer_t* tracer, const char* name, const char* parent_traceparent);

/**
 * @brief Set a string attribute on a span.
 * @param span Target span
 * @param key Attribute key
 * @param val String value
 */
void cdsl_trace_set_attr_string(cdsl_trace_span_t* span, const char* key, const char* val);

/**
 * @brief Set an integer attribute on a span.
 * @param span Target span
 * @param key Attribute key
 * @param val Integer value
 */
void cdsl_trace_set_attr_int(cdsl_trace_span_t* span, const char* key, int64_t val);

/**
 * @brief Set a floating point attribute on a span.
 * @param span Target span
 * @param key Attribute key
 * @param val Double value
 */
void cdsl_trace_set_attr_double(cdsl_trace_span_t* span, const char* key, double val);

/**
 * @brief End a span, stamping its end timestamp and recording it in the tracer.
 * @param span Target span
 * @param status Span status code
 * @param description Optional error or status description (may be NULL)
 */
void
cdsl_trace_end_span(cdsl_trace_span_t* span, cdsl_span_status_t status, const char* description);

/**
 * @brief Generate a W3C traceparent header string for the span.
 *
 * Format: "00-{trace_id}-{span_id}-01"
 *
 * @param span Target span
 * @param out_buffer Buffer of size at least 56 bytes
 * @param buffer_size Size of out_buffer
 * @return 1 on success, 0 if buffer too small
 */
int cdsl_trace_get_traceparent(const cdsl_trace_span_t* span, char* out_buffer, size_t buffer_size);

/**
 * @brief Export all completed spans in standard OpenTelemetry JSON format.
 *
 * @param tracer Tracer
 * @return Newly malloc'd JSON string (caller must free())
 */
char* cdsl_trace_export_json(const cdsl_tracer_t* tracer);

#ifdef __cplusplus
}
#endif

#endif /* CDSL_TRACE_H */
