/**
 * @file src/obs/trace.c
 * @brief OpenTelemetry decision tracing and W3C distributed trace context.
 */

#include "cdsl/trace.h"
#include "cdsl/util/threads.h"
#include "cdsl/util/portability.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

struct cdsl_tracer {
	char* service_name;
	cdsl_trace_span_t* spans;
	cdsl_mutex_t lock;
};

static uint64_t
get_unix_nano(void)
{
#if defined(_WIN32)
	FILETIME ft;
	GetSystemTimeAsFileTime(&ft);
	ULARGE_INTEGER li;
	li.LowPart = ft.dwLowDateTime;
	li.HighPart = ft.dwHighDateTime;
	return (li.QuadPart - 116444736000000000ULL) * 100ULL;
#elif defined(CLOCK_REALTIME)
	struct timespec ts;
	clock_gettime(CLOCK_REALTIME, &ts);
	return ((uint64_t)ts.tv_sec * 1000000000ULL) + (uint64_t)ts.tv_nsec;
#else
	return (uint64_t)time(NULL) * 1000000000ULL;
#endif
}

static void
random_hex(char* out, size_t num_bytes)
{
	static const char hex[] = "0123456789abcdef";
	for (size_t i = 0; i < num_bytes; i++) {
		unsigned int r = (unsigned int)rand() % 256;
		out[i * 2] = hex[(r >> 4) & 0x0F];
		out[i * 2 + 1] = hex[r & 0x0F];
	}
	out[num_bytes * 2] = '\0';
}

cdsl_tracer_t*
cdsl_tracer_create(const char* service_name)
{
	cdsl_tracer_t* t = calloc(1, sizeof(*t));
	if (!t) {
		return NULL;
	}
	t->service_name = cdsl_strdup(service_name ? service_name : "cdsl-engine");
	if (CDSL_MUTEX_INIT(&t->lock) != 0) {
		free(t->service_name);
		free(t);
		return NULL;
	}
	return t;
}

static void
span_attr_free(cdsl_span_attr_t* a)
{
	while (a) {
		cdsl_span_attr_t* next = a->next;
		if (a->key) {
			free(a->key);
		}
		if (a->string_val) {
			free(a->string_val);
		}
		free(a);
		a = next;
	}
}

static void
span_free(cdsl_trace_span_t* s)
{
	if (!s) {
		return;
	}
	if (s->name) {
		free(s->name);
	}
	if (s->status_description) {
		free(s->status_description);
	}
	span_attr_free(s->attributes);
	free(s);
}

void
cdsl_tracer_clear(cdsl_tracer_t* tracer)
{
	if (!tracer) {
		return;
	}
	CDSL_MUTEX_LOCK(&tracer->lock);
	cdsl_trace_span_t* s = tracer->spans;
	while (s) {
		cdsl_trace_span_t* next = s->next;
		span_free(s);
		s = next;
	}
	tracer->spans = NULL;
	CDSL_MUTEX_UNLOCK(&tracer->lock);
}

void
cdsl_tracer_free(cdsl_tracer_t* tracer)
{
	if (!tracer) {
		return;
	}
	cdsl_tracer_clear(tracer);
	CDSL_MUTEX_DESTROY(&tracer->lock);
	if (tracer->service_name) {
		free(tracer->service_name);
	}
	free(tracer);
}

cdsl_trace_span_t*
cdsl_trace_start_span(cdsl_tracer_t* tracer, const char* name, const char* parent_traceparent)
{
	if (!tracer || !name) {
		return NULL;
	}
	cdsl_trace_span_t* span = calloc(1, sizeof(*span));
	if (!span) {
		return NULL;
	}
	span->name = cdsl_strdup(name);
	span->start_time_unix_nano = get_unix_nano();

	/* Parse parent W3C traceparent or generate new trace ID */
	int parsed_parent = 0;
	if (parent_traceparent && strlen(parent_traceparent) >= 55) {
		/* Format: 00-4bf92f3577b34da6a3ce929d0e0e4736-00f067aa0ba902b7-01 */
		if (strncmp(parent_traceparent, "00-", 3) == 0 && parent_traceparent[35] == '-' &&
		    parent_traceparent[52] == '-') {
			memcpy(span->trace_id, parent_traceparent + 3, 32);
			span->trace_id[32] = '\0';
			memcpy(span->parent_span_id, parent_traceparent + 36, 16);
			span->parent_span_id[16] = '\0';
			parsed_parent = 1;
		}
	}

	if (!parsed_parent) {
		random_hex(span->trace_id, 16);
		span->parent_span_id[0] = '\0';
	}

	random_hex(span->span_id, 8);

	CDSL_MUTEX_LOCK(&tracer->lock);
	span->next = tracer->spans;
	tracer->spans = span;
	CDSL_MUTEX_UNLOCK(&tracer->lock);

	return span;
}

void
cdsl_trace_set_attr_string(cdsl_trace_span_t* span, const char* key, const char* val)
{
	if (!span || !key) {
		return;
	}
	cdsl_span_attr_t* a = calloc(1, sizeof(*a));
	if (!a) {
		return;
	}
	a->key = cdsl_strdup(key);
	a->string_val = val ? cdsl_strdup(val) : cdsl_strdup("");
	a->val_type = 0;
	a->next = span->attributes;
	span->attributes = a;
}

void
cdsl_trace_set_attr_int(cdsl_trace_span_t* span, const char* key, int64_t val)
{
	if (!span || !key) {
		return;
	}
	cdsl_span_attr_t* a = calloc(1, sizeof(*a));
	if (!a) {
		return;
	}
	a->key = cdsl_strdup(key);
	a->int_val = val;
	a->val_type = 1;
	a->next = span->attributes;
	span->attributes = a;
}

void
cdsl_trace_set_attr_double(cdsl_trace_span_t* span, const char* key, double val)
{
	if (!span || !key) {
		return;
	}
	cdsl_span_attr_t* a = calloc(1, sizeof(*a));
	if (!a) {
		return;
	}
	a->key = cdsl_strdup(key);
	a->double_val = val;
	a->val_type = 2;
	a->next = span->attributes;
	span->attributes = a;
}

void
cdsl_trace_end_span(cdsl_trace_span_t* span, cdsl_span_status_t status, const char* description)
{
	if (!span) {
		return;
	}
	span->end_time_unix_nano = get_unix_nano();
	span->status = status;
	if (description) {
		span->status_description = cdsl_strdup(description);
	}
}

int
cdsl_trace_get_traceparent(const cdsl_trace_span_t* span, char* out_buffer, size_t buffer_size)
{
	if (!span || !out_buffer || buffer_size < 56) {
		return 0;
	}
	snprintf(out_buffer, buffer_size, "00-%s-%s-01", span->trace_id, span->span_id);
	return 1;
}

static void
tbuf_append(char** pbuf, size_t* pcap, size_t* plen, const char* str)
{
	size_t slen = strlen(str);
	if (*plen + slen + 1 >= *pcap) {
		size_t ncap = (*pcap + slen + 1) * 2;
		char* nb = realloc(*pbuf, ncap);
		if (!nb) {
			return;
		}
		*pbuf = nb;
		*pcap = ncap;
	}
	memcpy(*pbuf + *plen, str, slen);
	*plen += slen;
	(*pbuf)[*plen] = '\0';
}

char*
cdsl_trace_export_json(const cdsl_tracer_t* tracer)
{
	if (!tracer) {
		return NULL;
	}
	size_t cap = 4096;
	size_t len = 0;
	char* buf = malloc(cap);
	if (!buf) {
		return NULL;
	}
	buf[0] = '\0';

	char line[512];
	snprintf(line,
		 sizeof(line),
		 "{\n"
		 "  \"resourceSpans\": [{\n"
		 "    \"resource\": {\n"
		 "      \"attributes\": [{\"key\": \"service.name\", \"value\": {\"stringValue\": "
		 "\"%s\"}}]\n"
		 "    },\n"
		 "    \"scopeSpans\": [{\n"
		 "      \"scope\": {\"name\": \"cdsl.decision\"},\n"
		 "      \"spans\": [\n",
		 tracer->service_name ? tracer->service_name : "cdsl");
	tbuf_append(&buf, &cap, &len, line);

	int first_span = 1;
	for (cdsl_trace_span_t* s = tracer->spans; s; s = s->next) {
		if (!first_span) {
			tbuf_append(&buf, &cap, &len, ",\n");
		}
		first_span = 0;

		snprintf(line,
			 sizeof(line),
			 "        {\n"
			 "          \"traceId\": \"%s\",\n"
			 "          \"spanId\": \"%s\",\n"
			 "          \"parentSpanId\": \"%s\",\n"
			 "          \"name\": \"%s\",\n"
			 "          \"startTimeUnixNano\": \"%llu\",\n"
			 "          \"endTimeUnixNano\": \"%llu\",\n"
			 "          \"status\": {\"code\": %d},\n"
			 "          \"attributes\": [",
			 s->trace_id,
			 s->span_id,
			 s->parent_span_id,
			 s->name ? s->name : "",
			 (unsigned long long)s->start_time_unix_nano,
			 (unsigned long long)s->end_time_unix_nano,
			 (int)s->status);
		tbuf_append(&buf, &cap, &len, line);

		int first_attr = 1;
		for (cdsl_span_attr_t* a = s->attributes; a; a = a->next) {
			if (!first_attr) {
				tbuf_append(&buf, &cap, &len, ", ");
			}
			first_attr = 0;

			if (a->val_type == 0) {
				snprintf(line,
					 sizeof(line),
					 "{\"key\": \"%s\", \"value\": {\"stringValue\": \"%s\"}}",
					 a->key,
					 a->string_val ? a->string_val : "");
			} else if (a->val_type == 1) {
				snprintf(line,
					 sizeof(line),
					 "{\"key\": \"%s\", \"value\": {\"intValue\": \"%lld\"}}",
					 a->key,
					 (long long)a->int_val);
			} else {
				snprintf(line,
					 sizeof(line),
					 "{\"key\": \"%s\", \"value\": {\"doubleValue\": %.4f}}",
					 a->key,
					 a->double_val);
			}
			tbuf_append(&buf, &cap, &len, line);
		}
		tbuf_append(&buf, &cap, &len, "]\n        }");
	}

	tbuf_append(&buf,
		    &cap,
		    &len,
		    "\n"
		    "      ]\n"
		    "    }]\n"
		    "  }]\n"
		    "}\n");

	return buf;
}
