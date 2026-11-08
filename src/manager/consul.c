/**
 * @file src/manager/consul.c
 * @brief Consul KV in-memory simulation configuration source driver.
 *
 * Simulates Consul KV ModifyIndex semantics in-memory without external network dependencies.
 */

#include "cdsl/config_source.h"
#include "cdsl/util/threads.h"
#include "cdsl/util/portability.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct consul_entry {
	char* key;
	char* content;
	size_t len;
	char* modify_index;
	struct consul_entry* next;
} consul_entry_t;

typedef struct consul_watcher {
	char* key;
	cdsl_config_watch_fn callback;
	void* user_data;
	struct consul_watcher* next;
} consul_watcher_t;

typedef struct consul_source_data {
	char* endpoint;
	char* key_prefix;
	cdsl_mutex_t lock;
	consul_entry_t* entries;
	consul_watcher_t* watchers;
	uint64_t modify_index;
} consul_source_data_t;

static const char*
consul_name(cdsl_config_source_t* src)
{
	(void)src;
	return "consul-memory";
}

static int
consul_fetch(cdsl_config_source_t* src,
	     const char* key,
	     char** out_content,
	     size_t* out_len,
	     char** out_version)
{
	if (!src || !src->driver_data || !key) {
		return -1;
	}
	consul_source_data_t* d = (consul_source_data_t*)src->driver_data;
	CDSL_MUTEX_LOCK(&d->lock);

	for (consul_entry_t* e = d->entries; e; e = e->next) {
		if (strcmp(e->key, key) == 0) {
			if (out_content) {
				char* copy = malloc(e->len + 1);
				if (!copy) {
					CDSL_MUTEX_UNLOCK(&d->lock);
					return -1;
				}
				memcpy(copy, e->content, e->len);
				copy[e->len] = '\0';
				*out_content = copy;
			}
			if (out_len) {
				*out_len = e->len;
			}
			if (out_version && e->modify_index) {
				*out_version = cdsl_strdup(e->modify_index);
			}
			CDSL_MUTEX_UNLOCK(&d->lock);
			return 0;
		}
	}

	CDSL_MUTEX_UNLOCK(&d->lock);
	return -1;
}

static int
consul_publish(
    cdsl_config_source_t* src, const char* key, const char* content, size_t len, char** out_version)
{
	if (!src || !src->driver_data || !key || !content) {
		return -1;
	}
	consul_source_data_t* d = (consul_source_data_t*)src->driver_data;
	CDSL_MUTEX_LOCK(&d->lock);

	d->modify_index++;
	char vbuf[32];
	snprintf(vbuf, sizeof(vbuf), "consul-idx-%llu", (unsigned long long)d->modify_index);

	consul_entry_t* target = NULL;
	for (consul_entry_t* e = d->entries; e; e = e->next) {
		if (strcmp(e->key, key) == 0) {
			target = e;
			break;
		}
	}

	if (!target) {
		target = calloc(1, sizeof(consul_entry_t));
		if (!target) {
			CDSL_MUTEX_UNLOCK(&d->lock);
			return -1;
		}
		target->key = cdsl_strdup(key);
		target->next = d->entries;
		d->entries = target;
	} else {
		free(target->content);
		free(target->modify_index);
	}

	target->content = malloc(len + 1);
	if (!target->content) {
		CDSL_MUTEX_UNLOCK(&d->lock);
		return -1;
	}
	memcpy(target->content, content, len);
	target->content[len] = '\0';
	target->len = len;
	target->modify_index = cdsl_strdup(vbuf);

	if (out_version) {
		*out_version = cdsl_strdup(vbuf);
	}

	/* Snapshot matching watchers under lock */
	size_t match_count = 0;
	for (consul_watcher_t* w = d->watchers; w; w = w->next) {
		if (strcmp(w->key, key) == 0 || strcmp(w->key, "*") == 0) {
			match_count++;
		}
	}

	typedef struct watcher_cb_entry {
		cdsl_config_watch_fn callback;
		void* user_data;
	} watcher_cb_entry_t;

	watcher_cb_entry_t stack_entries[16];
	watcher_cb_entry_t* dispatch_list = stack_entries;
	if (match_count > 16) {
		dispatch_list = malloc(match_count * sizeof(watcher_cb_entry_t));
		if (!dispatch_list) {
			dispatch_list = stack_entries;
			match_count = 16;
		}
	}

	size_t idx = 0;
	for (consul_watcher_t* w = d->watchers; w && idx < match_count; w = w->next) {
		if (strcmp(w->key, key) == 0 || strcmp(w->key, "*") == 0) {
			dispatch_list[idx].callback = w->callback;
			dispatch_list[idx].user_data = w->user_data;
			idx++;
		}
	}

	/* Snapshot content under lock: a concurrent publish of the same key can
	 * free target->content, so callbacks must only see this copy */
	char* content_snapshot = malloc(target->len + 1);
	if (!content_snapshot) {
		CDSL_MUTEX_UNLOCK(&d->lock);
		if (dispatch_list != stack_entries) {
			free(dispatch_list);
		}
		return -1;
	}
	memcpy(content_snapshot, target->content, target->len);
	content_snapshot[target->len] = '\0';
	size_t out_len = target->len;

	/* Release mutex BEFORE invoking user callbacks */
	CDSL_MUTEX_UNLOCK(&d->lock);

	/* Dispatch callbacks lock-free with the content snapshot */
	for (size_t i = 0; i < idx; i++) {
		if (dispatch_list[i].callback) {
			dispatch_list[i].callback(
			    key, content_snapshot, out_len, vbuf, dispatch_list[i].user_data);
		}
	}

	free(content_snapshot);
	if (dispatch_list != stack_entries) {
		free(dispatch_list);
	}

	return 0;
}

static int
consul_watch(cdsl_config_source_t* src,
	     const char* key,
	     cdsl_config_watch_fn callback,
	     void* user_data)
{
	if (!src || !src->driver_data || !key || !callback) {
		return -1;
	}
	consul_source_data_t* d = (consul_source_data_t*)src->driver_data;
	CDSL_MUTEX_LOCK(&d->lock);

	consul_watcher_t* w = calloc(1, sizeof(consul_watcher_t));
	if (!w) {
		CDSL_MUTEX_UNLOCK(&d->lock);
		return -1;
	}
	w->key = cdsl_strdup(key);
	w->callback = callback;
	w->user_data = user_data;
	w->next = d->watchers;
	d->watchers = w;

	CDSL_MUTEX_UNLOCK(&d->lock);
	return 0;
}

static void
consul_close(cdsl_config_source_t* src)
{
	if (!src || !src->driver_data) {
		return;
	}
	consul_source_data_t* d = (consul_source_data_t*)src->driver_data;
	CDSL_MUTEX_LOCK(&d->lock);

	consul_entry_t* e = d->entries;
	while (e) {
		consul_entry_t* next = e->next;
		free(e->key);
		free(e->content);
		free(e->modify_index);
		free(e);
		e = next;
	}

	consul_watcher_t* w = d->watchers;
	while (w) {
		consul_watcher_t* next = w->next;
		free(w->key);
		free(w);
		w = next;
	}

	CDSL_MUTEX_UNLOCK(&d->lock);
	CDSL_MUTEX_DESTROY(&d->lock);

	free(d->endpoint);
	free(d->key_prefix);
	free(d);
	src->driver_data = NULL;
}

static const cdsl_config_source_ops_t s_consul_ops = {
    .name = consul_name,
    .fetch = consul_fetch,
    .publish = consul_publish,
    .watch = consul_watch,
    .close = consul_close,
};

cdsl_config_source_t*
cdsl_config_source_create_consul_mem(const char* key_prefix)
{
	return cdsl_config_source_create_consul(NULL, key_prefix);
}

cdsl_config_source_t*
cdsl_config_source_create_consul(const char* endpoint, const char* key_prefix)
{
	cdsl_config_source_t* src = calloc(1, sizeof(cdsl_config_source_t));
	if (!src) {
		return NULL;
	}
	consul_source_data_t* d = calloc(1, sizeof(consul_source_data_t));
	if (!d) {
		free(src);
		return NULL;
	}
	d->endpoint = cdsl_strdup(endpoint ? endpoint : "http://127.0.0.1:8500");
	d->key_prefix = cdsl_strdup(key_prefix ? key_prefix : "cdsl/rules/");
	CDSL_MUTEX_INIT(&d->lock);

	src->ops = &s_consul_ops;
	src->driver_data = d;
	return src;
}
