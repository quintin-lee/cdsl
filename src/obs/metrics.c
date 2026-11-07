/**
 * @file src/obs/metrics.c
 * @brief Prometheus metrics collector and exposition format exporter.
 */

#include "cdsl/metrics.h"
#include "cdsl/util/threads.h"
#include "cdsl/util/portability.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const double s_buckets[] = {0.0001, 0.0005, 0.001, 0.005, 0.01, 0.05, 0.1, 0.5, 1.0, 5.0};
#define NUM_BUCKETS (sizeof(s_buckets) / sizeof(s_buckets[0]))

typedef struct rule_metric {
	char* rule_name;
	uint64_t status_counts[4]; /* PASSED, PARTIALLY_PASSED, FAILED, ERROR */
	uint64_t bucket_counts[NUM_BUCKETS];
	uint64_t total_count;
	double sum_duration;
	struct rule_metric* next;
} rule_metric_t;

typedef struct gauge_metric {
	char* name;
	double value;
	struct gauge_metric* next;
} gauge_metric_t;

typedef struct custom_counter {
	char* name;
	char* label_key;
	char* label_val;
	uint64_t count;
	struct custom_counter* next;
} custom_counter_t;

struct cdsl_metrics_collector {
	rule_metric_t* rules;
	gauge_metric_t* gauges;
	custom_counter_t* counters;
	cdsl_mutex_t lock;
};

cdsl_metrics_collector_t*
cdsl_metrics_create(void)
{
	cdsl_metrics_collector_t* c = calloc(1, sizeof(*c));
	if (!c) {
		return NULL;
	}
	if (CDSL_MUTEX_INIT(&c->lock) != 0) {
		free(c);
		return NULL;
	}
	return c;
}

void
cdsl_metrics_reset(cdsl_metrics_collector_t* c)
{
	if (!c) {
		return;
	}
	CDSL_MUTEX_LOCK(&c->lock);

	rule_metric_t* r = c->rules;
	while (r) {
		rule_metric_t* next = r->next;
		free(r->rule_name);
		free(r);
		r = next;
	}
	c->rules = NULL;

	gauge_metric_t* g = c->gauges;
	while (g) {
		gauge_metric_t* next = g->next;
		free(g->name);
		free(g);
		g = next;
	}
	c->gauges = NULL;

	custom_counter_t* cnt = c->counters;
	while (cnt) {
		custom_counter_t* next = cnt->next;
		free(cnt->name);
		free(cnt->label_key);
		free(cnt->label_val);
		free(cnt);
		cnt = next;
	}
	c->counters = NULL;

	CDSL_MUTEX_UNLOCK(&c->lock);
}

void
cdsl_metrics_free(cdsl_metrics_collector_t* c)
{
	if (!c) {
		return;
	}
	cdsl_metrics_reset(c);
	CDSL_MUTEX_DESTROY(&c->lock);
	free(c);
}

static rule_metric_t*
find_or_create_rule(cdsl_metrics_collector_t* c, const char* rule_name)
{
	for (rule_metric_t* cur = c->rules; cur; cur = cur->next) {
		if (strcmp(cur->rule_name, rule_name) == 0) {
			return cur;
		}
	}
	rule_metric_t* rm = calloc(1, sizeof(*rm));
	if (!rm) {
		return NULL;
	}
	rm->rule_name = cdsl_strdup(rule_name);
	rm->next = c->rules;
	c->rules = rm;
	return rm;
}

void
cdsl_metrics_record_rule(cdsl_metrics_collector_t* c,
			 const char* rule_name,
			 cdsl_rule_status_t status,
			 double duration_seconds)
{
	if (!c || !rule_name) {
		return;
	}
	CDSL_MUTEX_LOCK(&c->lock);
	rule_metric_t* rm = find_or_create_rule(c, rule_name);
	if (rm) {
		int st_idx = (int)status;
		if (st_idx >= 0 && st_idx < 4) {
			rm->status_counts[st_idx]++;
		}
		rm->total_count++;
		rm->sum_duration += duration_seconds;
		for (size_t i = 0; i < NUM_BUCKETS; i++) {
			if (duration_seconds <= s_buckets[i]) {
				rm->bucket_counts[i]++;
			}
		}
	}
	CDSL_MUTEX_UNLOCK(&c->lock);
}

void
cdsl_metrics_inc(cdsl_metrics_collector_t* c,
		 const char* metric_name,
		 const char* label_key,
		 const char* label_val)
{
	if (!c || !metric_name) {
		return;
	}
	CDSL_MUTEX_LOCK(&c->lock);
	for (custom_counter_t* cur = c->counters; cur; cur = cur->next) {
		if (strcmp(cur->name, metric_name) == 0 &&
		    ((!cur->label_key && !label_key) ||
		     (cur->label_key && label_key && strcmp(cur->label_key, label_key) == 0)) &&
		    ((!cur->label_val && !label_val) ||
		     (cur->label_val && label_val && strcmp(cur->label_val, label_val) == 0))) {
			cur->count++;
			CDSL_MUTEX_UNLOCK(&c->lock);
			return;
		}
	}
	custom_counter_t* cnt = calloc(1, sizeof(*cnt));
	if (cnt) {
		cnt->name = cdsl_strdup(metric_name);
		cnt->label_key = label_key ? cdsl_strdup(label_key) : NULL;
		cnt->label_val = label_val ? cdsl_strdup(label_val) : NULL;
		cnt->count = 1;
		cnt->next = c->counters;
		c->counters = cnt;
	}
	CDSL_MUTEX_UNLOCK(&c->lock);
}

void
cdsl_metrics_set_gauge(cdsl_metrics_collector_t* c, const char* gauge_name, double value)
{
	if (!c || !gauge_name) {
		return;
	}
	CDSL_MUTEX_LOCK(&c->lock);
	for (gauge_metric_t* cur = c->gauges; cur; cur = cur->next) {
		if (strcmp(cur->name, gauge_name) == 0) {
			cur->value = value;
			CDSL_MUTEX_UNLOCK(&c->lock);
			return;
		}
	}
	gauge_metric_t* gm = calloc(1, sizeof(*gm));
	if (gm) {
		gm->name = cdsl_strdup(gauge_name);
		gm->value = value;
		gm->next = c->gauges;
		c->gauges = gm;
	}
	CDSL_MUTEX_UNLOCK(&c->lock);
}

static void
buf_append(char** pbuf, size_t* pcap, size_t* plen, const char* str)
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
cdsl_metrics_export_prometheus(const cdsl_metrics_collector_t* c)
{
	if (!c) {
		return NULL;
	}
	size_t cap = 2048;
	size_t len = 0;
	char* buf = malloc(cap);
	if (!buf) {
		return NULL;
	}
	buf[0] = '\0';

	char line[512];

	/* Header for evaluations */
	buf_append(&buf,
		   &cap,
		   &len,
		   "# HELP cdsl_rule_evaluations_total Total rule evaluation count by status\n"
		   "# TYPE cdsl_rule_evaluations_total counter\n");

	static const char* status_names[] = {"passed", "partially_passed", "failed", "error"};

	for (rule_metric_t* rm = c->rules; rm; rm = rm->next) {
		for (int s = 0; s < 4; s++) {
			if (rm->status_counts[s] > 0) {
				snprintf(line,
					 sizeof(line),
					 "cdsl_rule_evaluations_total{rule=\"%s\",status=\"%s\"} "
					 "%llu\n",
					 rm->rule_name,
					 status_names[s],
					 (unsigned long long)rm->status_counts[s]);
				buf_append(&buf, &cap, &len, line);
			}
		}
	}

	/* Latency histograms */
	buf_append(&buf,
		   &cap,
		   &len,
		   "# HELP cdsl_rule_eval_duration_seconds Rule execution latency in seconds\n"
		   "# TYPE cdsl_rule_eval_duration_seconds histogram\n");

	for (rule_metric_t* rm = c->rules; rm; rm = rm->next) {
		for (size_t b = 0; b < NUM_BUCKETS; b++) {
			snprintf(line,
				 sizeof(line),
				 "cdsl_rule_eval_duration_seconds_bucket{rule=\"%s\",le=\"%.4f\"} "
				 "%llu\n",
				 rm->rule_name,
				 s_buckets[b],
				 (unsigned long long)rm->bucket_counts[b]);
			buf_append(&buf, &cap, &len, line);
		}
		snprintf(line,
			 sizeof(line),
			 "cdsl_rule_eval_duration_seconds_bucket{rule=\"%s\",le=\"+Inf\"} %llu\n",
			 rm->rule_name,
			 (unsigned long long)rm->total_count);
		buf_append(&buf, &cap, &len, line);

		snprintf(line,
			 sizeof(line),
			 "cdsl_rule_eval_duration_seconds_sum{rule=\"%s\"} %.6f\n",
			 rm->rule_name,
			 rm->sum_duration);
		buf_append(&buf, &cap, &len, line);

		snprintf(line,
			 sizeof(line),
			 "cdsl_rule_eval_duration_seconds_count{rule=\"%s\"} %llu\n",
			 rm->rule_name,
			 (unsigned long long)rm->total_count);
		buf_append(&buf, &cap, &len, line);
	}

	/* Gauges */
	for (gauge_metric_t* gm = c->gauges; gm; gm = gm->next) {
		snprintf(line,
			 sizeof(line),
			 "# TYPE %s gauge\n%s %.4f\n",
			 gm->name,
			 gm->name,
			 gm->value);
		buf_append(&buf, &cap, &len, line);
	}

	/* Custom counters */
	for (custom_counter_t* cnt = c->counters; cnt; cnt = cnt->next) {
		if (cnt->label_key && cnt->label_val) {
			snprintf(line,
				 sizeof(line),
				 "%s{%s=\"%s\"} %llu\n",
				 cnt->name,
				 cnt->label_key,
				 cnt->label_val,
				 (unsigned long long)cnt->count);
		} else {
			snprintf(line,
				 sizeof(line),
				 "%s %llu\n",
				 cnt->name,
				 (unsigned long long)cnt->count);
		}
		buf_append(&buf, &cap, &len, line);
	}

	return buf;
}
