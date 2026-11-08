/**
 * @file src/manager/config_source.c
 * @brief Generic configuration source dispatcher and in-memory mock driver.
 */

#include "cdsl/config_source.h"
#include "cdsl/util/threads.h"
#include "cdsl/util/portability.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* ---- Generic dispatchers ---- */

int
cdsl_config_source_fetch(cdsl_config_source_t* src,
			 const char* key,
			 char** out_content,
			 size_t* out_len,
			 char** out_version)
{
	if (!src || !src->ops || !src->ops->fetch) {
		return -1;
	}
	return src->ops->fetch(src, key, out_content, out_len, out_version);
}

int
cdsl_config_source_publish(
    cdsl_config_source_t* src, const char* key, const char* content, size_t len, char** out_version)
{
	if (!src || !src->ops || !src->ops->publish) {
		return -1;
	}
	return src->ops->publish(src, key, content, len, out_version);
}

int
cdsl_config_source_watch(cdsl_config_source_t* src,
			 const char* key,
			 cdsl_config_watch_fn callback,
			 void* user_data)
{
	if (!src || !src->ops || !src->ops->watch) {
		return -1;
	}
	return src->ops->watch(src, key, callback, user_data);
}

void
cdsl_config_source_free(cdsl_config_source_t* src)
{
	if (!src) {
		return;
	}
	if (src->ops && src->ops->close) {
		src->ops->close(src);
	}
	free(src);
}

const char*
cdsl_config_source_get_name(const cdsl_config_source_t* src)
{
	if (!src || !src->ops || !src->ops->name) {
		return "unknown";
	}
	return src->ops->name((cdsl_config_source_t*)src);
}

/* ---- In-Memory Driver ---- */

typedef struct mem_entry {
	char* key;
	char* content;
	size_t len;
	uint64_t revision;
	struct mem_entry* next;
} mem_entry_t;

typedef struct mem_watcher {
	char* key;
	cdsl_config_watch_fn callback;
	void* user_data;
	struct mem_watcher* next;
} mem_watcher_t;

typedef struct mem_source_data {
	cdsl_mutex_t lock;
	mem_entry_t* entries;
	mem_watcher_t* watchers;
	uint64_t global_rev;
} mem_source_data_t;

static const char*
mem_driver_name(cdsl_config_source_t* src)
{
	(void)src;
	return "memory";
}

static int
mem_driver_fetch(cdsl_config_source_t* src,
		 const char* key,
		 char** out_content,
		 size_t* out_len,
		 char** out_version)
{
	if (!src || !src->driver_data || !key) {
		return -1;
	}
	mem_source_data_t* data = (mem_source_data_t*)src->driver_data;
	CDSL_MUTEX_LOCK(&data->lock);

	for (mem_entry_t* e = data->entries; e; e = e->next) {
		if (strcmp(e->key, key) == 0) {
			if (out_content) {
				char* copy = malloc(e->len + 1);
				if (!copy) {
					CDSL_MUTEX_UNLOCK(&data->lock);
					return -1;
				}
				memcpy(copy, e->content, e->len);
				copy[e->len] = '\0';
				*out_content = copy;
			}
			if (out_len) {
				*out_len = e->len;
			}
			if (out_version) {
				char vbuf[32];
				snprintf(
				    vbuf, sizeof(vbuf), "%llu", (unsigned long long)e->revision);
				*out_version = cdsl_strdup(vbuf);
			}
			CDSL_MUTEX_UNLOCK(&data->lock);
			return 0;
		}
	}

	CDSL_MUTEX_UNLOCK(&data->lock);
	return -1; /* Not found */
}

static int
mem_driver_publish(
    cdsl_config_source_t* src, const char* key, const char* content, size_t len, char** out_version)
{
	if (!src || !src->driver_data || !key || !content) {
		return -1;
	}
	mem_source_data_t* data = (mem_source_data_t*)src->driver_data;
	CDSL_MUTEX_LOCK(&data->lock);

	data->global_rev++;
	mem_entry_t* target = NULL;
	for (mem_entry_t* e = data->entries; e; e = e->next) {
		if (strcmp(e->key, key) == 0) {
			target = e;
			break;
		}
	}

	if (!target) {
		target = calloc(1, sizeof(mem_entry_t));
		if (!target) {
			CDSL_MUTEX_UNLOCK(&data->lock);
			return -1;
		}
		target->key = cdsl_strdup(key);
		target->next = data->entries;
		data->entries = target;
	} else {
		free(target->content);
	}

	target->content = malloc(len + 1);
	if (!target->content) {
		CDSL_MUTEX_UNLOCK(&data->lock);
		return -1;
	}
	memcpy(target->content, content, len);
	target->content[len] = '\0';
	target->len = len;
	target->revision = data->global_rev;

	char vbuf[32];
	snprintf(vbuf, sizeof(vbuf), "%llu", (unsigned long long)target->revision);
	if (out_version) {
		*out_version = cdsl_strdup(vbuf);
	}

	/* Snapshot matching watchers under lock */
	size_t match_count = 0;
	for (mem_watcher_t* w = data->watchers; w; w = w->next) {
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
	for (mem_watcher_t* w = data->watchers; w && idx < match_count; w = w->next) {
		if (strcmp(w->key, key) == 0 || strcmp(w->key, "*") == 0) {
			dispatch_list[idx].callback = w->callback;
			dispatch_list[idx].user_data = w->user_data;
			idx++;
		}
	}

	const char* out_content_ptr = target->content;
	size_t out_len = target->len;

	/* Release mutex BEFORE invoking user callbacks */
	CDSL_MUTEX_UNLOCK(&data->lock);

	/* Dispatch callbacks lock-free */
	for (size_t i = 0; i < idx; i++) {
		if (dispatch_list[i].callback) {
			dispatch_list[i].callback(
			    key, out_content_ptr, out_len, vbuf, dispatch_list[i].user_data);
		}
	}

	if (dispatch_list != stack_entries) {
		free(dispatch_list);
	}

	return 0;
}

static int
mem_driver_watch(cdsl_config_source_t* src,
		 const char* key,
		 cdsl_config_watch_fn callback,
		 void* user_data)
{
	if (!src || !src->driver_data || !key || !callback) {
		return -1;
	}
	mem_source_data_t* data = (mem_source_data_t*)src->driver_data;
	CDSL_MUTEX_LOCK(&data->lock);

	mem_watcher_t* w = calloc(1, sizeof(mem_watcher_t));
	if (!w) {
		CDSL_MUTEX_UNLOCK(&data->lock);
		return -1;
	}
	w->key = cdsl_strdup(key);
	w->callback = callback;
	w->user_data = user_data;
	w->next = data->watchers;
	data->watchers = w;

	CDSL_MUTEX_UNLOCK(&data->lock);
	return 0;
}

static void
mem_driver_close(cdsl_config_source_t* src)
{
	if (!src || !src->driver_data) {
		return;
	}
	mem_source_data_t* data = (mem_source_data_t*)src->driver_data;
	CDSL_MUTEX_LOCK(&data->lock);

	mem_entry_t* e = data->entries;
	while (e) {
		mem_entry_t* next = e->next;
		free(e->key);
		free(e->content);
		free(e);
		e = next;
	}
	mem_watcher_t* w = data->watchers;
	while (w) {
		mem_watcher_t* next = w->next;
		free(w->key);
		free(w);
		w = next;
	}

	CDSL_MUTEX_UNLOCK(&data->lock);
	CDSL_MUTEX_DESTROY(&data->lock);
	free(data);
	src->driver_data = NULL;
}

static const cdsl_config_source_ops_t s_mem_ops = {
    .name = mem_driver_name,
    .fetch = mem_driver_fetch,
    .publish = mem_driver_publish,
    .watch = mem_driver_watch,
    .close = mem_driver_close,
};

cdsl_config_source_t*
cdsl_config_source_create_mem(void)
{
	cdsl_config_source_t* src = calloc(1, sizeof(cdsl_config_source_t));
	if (!src) {
		return NULL;
	}
	mem_source_data_t* data = calloc(1, sizeof(mem_source_data_t));
	if (!data) {
		free(src);
		return NULL;
	}
	CDSL_MUTEX_INIT(&data->lock);
	src->ops = &s_mem_ops;
	src->driver_data = data;
	return src;
}
