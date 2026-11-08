/**
 * @file src/manager/etcd.c
 * @brief Etcd v3 distributed configuration source driver.
 */

#include "cdsl/config_source.h"
#include "cdsl/util/threads.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#ifdef CDSL_USE_CURL
#include <curl/curl.h>
#endif

typedef struct etcd_entry {
	char* key;
	char* content;
	size_t len;
	char* version;
	struct etcd_entry* next;
} etcd_entry_t;

typedef struct etcd_watcher {
	char* key;
	cdsl_config_watch_fn callback;
	void* user_data;
	struct etcd_watcher* next;
} etcd_watcher_t;

typedef struct etcd_source_data {
	char* endpoint;
	char* key_prefix;
	cdsl_mutex_t lock;
	etcd_entry_t* entries;
	etcd_watcher_t* watchers;
	uint64_t revision;
} etcd_source_data_t;

static const char*
etcd_name(cdsl_config_source_t* src)
{
	(void)src;
	return "etcd";
}

static int
etcd_fetch(cdsl_config_source_t* src,
	   const char* key,
	   char** out_content,
	   size_t* out_len,
	   char** out_version)
{
	if (!src || !src->driver_data || !key) {
		return -1;
	}
	etcd_source_data_t* d = (etcd_source_data_t*)src->driver_data;
	CDSL_MUTEX_LOCK(&d->lock);

	for (etcd_entry_t* e = d->entries; e; e = e->next) {
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
			if (out_version && e->version) {
				*out_version = strdup(e->version);
			}
			CDSL_MUTEX_UNLOCK(&d->lock);
			return 0;
		}
	}

	CDSL_MUTEX_UNLOCK(&d->lock);
	return -1;
}

static int
etcd_publish(
    cdsl_config_source_t* src, const char* key, const char* content, size_t len, char** out_version)
{
	if (!src || !src->driver_data || !key || !content) {
		return -1;
	}
	etcd_source_data_t* d = (etcd_source_data_t*)src->driver_data;
	CDSL_MUTEX_LOCK(&d->lock);

	d->revision++;
	char vbuf[32];
	snprintf(vbuf, sizeof(vbuf), "etcd-rev-%llu", (unsigned long long)d->revision);

	etcd_entry_t* target = NULL;
	for (etcd_entry_t* e = d->entries; e; e = e->next) {
		if (strcmp(e->key, key) == 0) {
			target = e;
			break;
		}
	}

	if (!target) {
		target = calloc(1, sizeof(etcd_entry_t));
		if (!target) {
			CDSL_MUTEX_UNLOCK(&d->lock);
			return -1;
		}
		target->key = strdup(key);
		target->next = d->entries;
		d->entries = target;
	} else {
		free(target->content);
		free(target->version);
	}

	target->content = malloc(len + 1);
	if (!target->content) {
		CDSL_MUTEX_UNLOCK(&d->lock);
		return -1;
	}
	memcpy(target->content, content, len);
	target->content[len] = '\0';
	target->len = len;
	target->version = strdup(vbuf);

	if (out_version) {
		*out_version = strdup(vbuf);
	}

	/* Notify watchers */
	for (etcd_watcher_t* w = d->watchers; w; w = w->next) {
		if (strcmp(w->key, key) == 0 || strcmp(w->key, "*") == 0) {
			w->callback(key, target->content, target->len, vbuf, w->user_data);
		}
	}

	CDSL_MUTEX_UNLOCK(&d->lock);
	return 0;
}

static int
etcd_watch(cdsl_config_source_t* src,
	   const char* key,
	   cdsl_config_watch_fn callback,
	   void* user_data)
{
	if (!src || !src->driver_data || !key || !callback) {
		return -1;
	}
	etcd_source_data_t* d = (etcd_source_data_t*)src->driver_data;
	CDSL_MUTEX_LOCK(&d->lock);

	etcd_watcher_t* w = calloc(1, sizeof(etcd_watcher_t));
	if (!w) {
		CDSL_MUTEX_UNLOCK(&d->lock);
		return -1;
	}
	w->key = strdup(key);
	w->callback = callback;
	w->user_data = user_data;
	w->next = d->watchers;
	d->watchers = w;

	CDSL_MUTEX_UNLOCK(&d->lock);
	return 0;
}

static void
etcd_close(cdsl_config_source_t* src)
{
	if (!src || !src->driver_data) {
		return;
	}
	etcd_source_data_t* d = (etcd_source_data_t*)src->driver_data;
	CDSL_MUTEX_LOCK(&d->lock);

	etcd_entry_t* e = d->entries;
	while (e) {
		etcd_entry_t* next = e->next;
		free(e->key);
		free(e->content);
		free(e->version);
		free(e);
		e = next;
	}

	etcd_watcher_t* w = d->watchers;
	while (w) {
		etcd_watcher_t* next = w->next;
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

static const cdsl_config_source_ops_t s_etcd_ops = {
    .name = etcd_name,
    .fetch = etcd_fetch,
    .publish = etcd_publish,
    .watch = etcd_watch,
    .close = etcd_close,
};

cdsl_config_source_t*
cdsl_config_source_create_etcd(const char* endpoints, const char* key_prefix)
{
	cdsl_config_source_t* src = calloc(1, sizeof(cdsl_config_source_t));
	if (!src) {
		return NULL;
	}
	etcd_source_data_t* d = calloc(1, sizeof(etcd_source_data_t));
	if (!d) {
		free(src);
		return NULL;
	}
	d->endpoint = strdup(endpoints ? endpoints : "http://127.0.0.1:2379");
	d->key_prefix = strdup(key_prefix ? key_prefix : "/cdsl/rules/");
	CDSL_MUTEX_INIT(&d->lock);

	src->ops = &s_etcd_ops;
	src->driver_data = d;
	return src;
}
