/**
 * @file src/manager/manager.c
 * @brief Enterprise Rule Manager with Copy-On-Write (RCU) hot reload and rollback.
 */

#include "cdsl/manager.h"
#include "cdsl/config_source.h"
#include "cdsl/canary.h"
#include "cdsl/util/threads.h"
#include "cdsl/util/portability.h"
#include <stdlib.h>
#include <string.h>

struct cdsl_manager {
	const cdsl_schema_t* schema;
	_Atomic(cdsl_snapshot_t*) active;
	cdsl_snapshot_t* history[CDSL_MAX_HISTORY_SNAPSHOTS];
	size_t history_count;
	uint64_t next_version;
	cdsl_mutex_t write_lock;
};

static void
snapshot_destroy(cdsl_snapshot_t* s)
{
	if (!s) {
		return;
	}
	if (s->tag) {
		free(s->tag);
	}
	if (s->ruleset) {
		cdsl_ruleset_free(s->ruleset);
	}
	free(s);
}

cdsl_manager_t*
cdsl_manager_create(const cdsl_schema_t* schema)
{
	cdsl_manager_t* mgr = calloc(1, sizeof(*mgr));
	if (!mgr) {
		return NULL;
	}
	mgr->schema = schema;
	mgr->next_version = 1;
	if (CDSL_MUTEX_INIT(&mgr->write_lock) != 0) {
		free(mgr);
		return NULL;
	}
	return mgr;
}

void
cdsl_manager_release(cdsl_manager_t* mgr, cdsl_snapshot_t* snapshot)
{
	(void)mgr;
	if (!snapshot) {
		return;
	}
	if (CDSL_ATOMIC_FETCH_SUB(&snapshot->ref_count, 1) == 1) {
		if (snapshot->retired) {
			snapshot_destroy(snapshot);
		}
	}
}

void
cdsl_manager_free(cdsl_manager_t* mgr)
{
	if (!mgr) {
		return;
	}
	CDSL_MUTEX_LOCK(&mgr->write_lock);

	if (mgr->active) {
		mgr->active->retired = 1;
		cdsl_manager_release(mgr, mgr->active);
		mgr->active = NULL;
	}

	for (size_t i = 0; i < mgr->history_count; i++) {
		if (mgr->history[i]) {
			mgr->history[i]->retired = 1;
			cdsl_manager_release(mgr, mgr->history[i]);
			mgr->history[i] = NULL;
		}
	}
	mgr->history_count = 0;

	CDSL_MUTEX_UNLOCK(&mgr->write_lock);
	CDSL_MUTEX_DESTROY(&mgr->write_lock);
	free(mgr);
}

int
cdsl_manager_deploy(cdsl_manager_t* mgr,
		    cdsl_ruleset_t* ruleset,
		    const char* tag,
		    uint64_t* out_version)
{
	if (!mgr || !ruleset) {
		return 0;
	}

	cdsl_snapshot_t* snap = calloc(1, sizeof(*snap));
	if (!snap) {
		return 0;
	}

	CDSL_MUTEX_LOCK(&mgr->write_lock);

	snap->version_id = mgr->next_version++;
	snap->tag = tag ? cdsl_strdup(tag) : NULL;
	snap->ruleset = ruleset;
	CDSL_ATOMIC_STORE(&snap->ref_count, 1);
	snap->created_at = time(NULL);

	cdsl_snapshot_t* old = mgr->active;
	if (old) {
		if (mgr->history_count >= CDSL_MAX_HISTORY_SNAPSHOTS) {
			cdsl_snapshot_t* oldest = mgr->history[0];
			for (size_t i = 0; i < CDSL_MAX_HISTORY_SNAPSHOTS - 1; i++) {
				mgr->history[i] = mgr->history[i + 1];
			}
			mgr->history[CDSL_MAX_HISTORY_SNAPSHOTS - 1] = old;
			if (oldest) {
				oldest->retired = 1;
				cdsl_manager_release(mgr, oldest);
			}
		} else {
			mgr->history[mgr->history_count++] = old;
		}
	}

	CDSL_ATOMIC_STORE(&mgr->active, snap);

	if (out_version) {
		*out_version = snap->version_id;
	}

	CDSL_MUTEX_UNLOCK(&mgr->write_lock);
	return 1;
}

int
cdsl_manager_deploy_dsl(cdsl_manager_t* mgr,
			const char* dsl_text,
			const char* tag,
			uint64_t* out_version,
			cdsl_error_list_t** out_err)
{
	if (!mgr || !dsl_text) {
		return 0;
	}

	cdsl_error_list_t* parse_err = NULL;
	cdsl_rule_t* rule = cdsl_parse_string(dsl_text, &parse_err);
	if (!rule || (parse_err && cdsl_error_list_has_errors(parse_err))) {
		if (rule) {
			cdsl_free_rule(rule);
		}
		if (out_err) {
			*out_err = parse_err;
		} else if (parse_err) {
			cdsl_error_list_free(parse_err);
		}
		return 0;
	}

	if (mgr->schema) {
		cdsl_error_list_t* verify_err = cdsl_verify_rule_detailed(rule, mgr->schema);
		if (verify_err && cdsl_error_list_has_errors(verify_err)) {
			cdsl_free_rule(rule);
			if (out_err) {
				*out_err = verify_err;
			} else {
				cdsl_error_list_free(verify_err);
			}
			if (parse_err) {
				cdsl_error_list_free(parse_err);
			}
			return 0;
		}
		if (verify_err) {
			cdsl_error_list_free(verify_err);
		}
	}
	if (parse_err) {
		cdsl_error_list_free(parse_err);
	}

	cdsl_ruleset_t* rs = cdsl_ruleset_create();
	if (!rs) {
		cdsl_free_rule(rule);
		return 0;
	}
	cdsl_ruleset_add(rs, rule, 0);

	return cdsl_manager_deploy(mgr, rs, tag, out_version);
}

cdsl_snapshot_t*
cdsl_manager_acquire(cdsl_manager_t* mgr)
{
	if (!mgr) {
		return NULL;
	}
	cdsl_snapshot_t* snap = CDSL_ATOMIC_LOAD(&mgr->active);
	if (!snap) {
		return NULL;
	}
	CDSL_ATOMIC_FETCH_ADD(&snap->ref_count, 1);
	return snap;
}

int
cdsl_manager_rollback(cdsl_manager_t* mgr, uint64_t target_version)
{
	if (!mgr) {
		return 0;
	}
	CDSL_MUTEX_LOCK(&mgr->write_lock);

	int found_idx = -1;
	for (size_t i = 0; i < mgr->history_count; i++) {
		if (mgr->history[i] && mgr->history[i]->version_id == target_version) {
			found_idx = (int)i;
			break;
		}
	}

	if (found_idx < 0) {
		CDSL_MUTEX_UNLOCK(&mgr->write_lock);
		return 0;
	}

	cdsl_snapshot_t* target = mgr->history[found_idx];
	cdsl_snapshot_t* current = mgr->active;

	/* Replace target in history with current */
	mgr->history[found_idx] = current;
	CDSL_ATOMIC_STORE(&mgr->active, target);

	CDSL_MUTEX_UNLOCK(&mgr->write_lock);
	return 1;
}

const cdsl_schema_t*
cdsl_manager_get_schema(const cdsl_manager_t* mgr)
{
	return mgr ? mgr->schema : NULL;
}

uint64_t
cdsl_manager_get_active_version(const cdsl_manager_t* mgr)
{
	if (!mgr) {
		return 0;
	}
	cdsl_manager_t* non_const = (cdsl_manager_t*)mgr;
	cdsl_snapshot_t* s = CDSL_ATOMIC_LOAD(&non_const->active);
	return s ? s->version_id : 0;
}

cdsl_ruleset_report_t*
cdsl_manager_execute(cdsl_manager_t* mgr, cdsl_vm_t* vm, cdsl_context_t* ctx)
{
	if (!mgr || !vm || !ctx) {
		return NULL;
	}
	cdsl_snapshot_t* snap = cdsl_manager_acquire(mgr);
	if (!snap || !snap->ruleset) {
		if (snap) {
			cdsl_manager_release(mgr, snap);
		}
		return NULL;
	}

	cdsl_ruleset_report_t* rpt = cdsl_vm_execute_ruleset(vm, snap->ruleset, ctx);
	cdsl_manager_release(mgr, snap);
	return rpt;
}

size_t
cdsl_manager_get_history(const cdsl_manager_t* mgr, uint64_t* out_versions, size_t max_count)
{
	if (!mgr || !out_versions || max_count == 0) {
		return 0;
	}
	size_t count = 0;
	for (size_t i = 0; i < mgr->history_count && count < max_count; i++) {
		if (mgr->history[i]) {
			out_versions[count++] = mgr->history[i]->version_id;
		}
	}
	return count;
}

typedef struct cdsl_mgr_source_binding {
	cdsl_manager_t* mgr;
	char* key;
} cdsl_mgr_source_binding_t;

static void
manager_config_source_cb(
    const char* key, const char* content, size_t len, const char* version, void* user_data)
{
	(void)len;
	(void)version;
	cdsl_mgr_source_binding_t* b = (cdsl_mgr_source_binding_t*)user_data;
	if (!b || !b->mgr || !content) {
		return;
	}
	cdsl_canary_report_t rpt;
	uint64_t vid = 0;
	(void)cdsl_manager_deploy_canary(b->mgr, content, key, NULL, 0, &rpt, &vid);
	cdsl_canary_report_free(&rpt);
}

int
cdsl_manager_attach_config_source(cdsl_manager_t* mgr, cdsl_config_source_t* src, const char* key)
{
	if (!mgr || !src || !key) {
		return -1;
	}

	/* 1. Fetch initial configuration if already present */
	char* init_content = NULL;
	size_t init_len = 0;
	char* init_version = NULL;
	if (cdsl_config_source_fetch(src, key, &init_content, &init_len, &init_version) == 0 &&
	    init_content) {
		cdsl_canary_report_t rpt;
		uint64_t vid = 0;
		(void)cdsl_manager_deploy_canary(mgr, init_content, key, NULL, 0, &rpt, &vid);
		cdsl_canary_report_free(&rpt);
		free(init_content);
		free(init_version);
	}

	/* 2. Bind watcher */
	cdsl_mgr_source_binding_t* b = calloc(1, sizeof(cdsl_mgr_source_binding_t));
	if (!b) {
		return -1;
	}
	b->mgr = mgr;
	b->key = strdup(key);

	int rc = cdsl_config_source_watch(src, key, manager_config_source_cb, b);
	if (rc != 0) {
		free(b->key);
		free(b);
		return -1;
	}
	return 0;
}
