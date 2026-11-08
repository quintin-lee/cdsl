/**
 * @file manager.h
 * @brief Enterprise Rule Manager with Copy-On-Write (RCU) hot reload and instant rollback.
 *
 * Provides thread-safe, lock-free reader execution of rulesets with atomic
 * deployment of new versions and safe multi-version history retention.
 */

#ifndef CDSL_MANAGER_H
#define CDSL_MANAGER_H

#include "cdsl/schema.h"
#include "cdsl/ruleset.h"
#include "cdsl/vm.h"
#include "cdsl/util/portability.h"
#include <stddef.h>
#include <stdint.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CDSL_MAX_HISTORY_SNAPSHOTS 8

/**
 * @brief Versioned snapshot of a deployed ruleset.
 */
typedef struct cdsl_snapshot {
	uint64_t version_id;	     /**< Monotonically increasing version ID */
	char* tag;		     /**< Optional descriptive tag / label */
	cdsl_ruleset_t* ruleset;     /**< Compiled ruleset owned by this snapshot */
	cdsl_atomic_int_t ref_count; /**< Active in-flight reader references */
	time_t created_at;	     /**< Timestamp when snapshot was created */
	int retired;		     /**< 1 if evicted from history ring buffer */
	struct cdsl_snapshot* next;  /**< Linked list for history or retirement */
} cdsl_snapshot_t;

/**
 * @brief Enterprise Rule Manager handle.
 */
typedef struct cdsl_manager cdsl_manager_t;

/**
 * @brief Create a new Rule Manager bound to a schema.
 * @param schema Schema for rule validation (caller retains ownership or passes NULL)
 * @return Newly allocated manager (free with cdsl_manager_free)
 */
cdsl_manager_t* cdsl_manager_create(const cdsl_schema_t* schema);

/**
 * @brief Free a Rule Manager and all retained snapshots.
 *
 * Waits for active readers to finish before freeing underlying rulesets.
 *
 * @param mgr Manager to free (NULL-safe)
 */
void cdsl_manager_free(cdsl_manager_t* mgr);

/**
 * @brief Deploy a pre-built ruleset as the active version.
 *
 * Atomically swaps the active ruleset. Existing readers continue executing
 * their acquired snapshot without disruption.
 *
 * @param mgr Manager
 * @param ruleset Ruleset to deploy (manager assumes ownership on success)
 * @param tag Optional human-readable tag/version label (e.g. "v1.4-prod")
 * @param out_version Pointer to receive the assigned version ID (optional, may be NULL)
 * @return 1 on success, 0 on failure
 */
int cdsl_manager_deploy(cdsl_manager_t* mgr,
			cdsl_ruleset_t* ruleset,
			const char* tag,
			uint64_t* out_version);

/**
 * @brief Parse, verify, and deploy a DSL script text as a new version.
 *
 * @param mgr Manager
 * @param dsl_text DSL source code
 * @param tag Optional tag
 * @param out_version Output assigned version ID (optional)
 * @param out_err Output error report on failure (optional)
 * @return 1 on success, 0 on failure
 */
int cdsl_manager_deploy_dsl(cdsl_manager_t* mgr,
			    const char* dsl_text,
			    const char* tag,
			    uint64_t* out_version,
			    cdsl_error_list_t** out_err);

/**
 * @brief Acquire a read reference to the currently active snapshot (lock-free).
 *
 * Readers must call cdsl_manager_release() when finished evaluating the snapshot.
 *
 * @param mgr Manager
 * @return Active snapshot handle, or NULL if no version has been deployed
 */
cdsl_snapshot_t* cdsl_manager_acquire(cdsl_manager_t* mgr);

/**
 * @brief Release a previously acquired snapshot reference.
 *
 * @param mgr Manager
 * @param snapshot Snapshot to release
 */
void cdsl_manager_release(cdsl_manager_t* mgr, cdsl_snapshot_t* snapshot);

/**
 * @brief Get the schema associated with the manager.
 * @param mgr Manager
 * @return Schema pointer (may be NULL)
 */
const cdsl_schema_t* cdsl_manager_get_schema(const cdsl_manager_t* mgr);

/**
 * @brief Roll back to a previous snapshot by version ID.
 *
 * Searches the historical snapshot ring buffer and atomically restores the target
 * version as the active version.
 *
 * @param mgr Manager
 * @param target_version Version ID to restore
 * @return 1 on success, 0 if version not found or error
 */
int cdsl_manager_rollback(cdsl_manager_t* mgr, uint64_t target_version);

/**
 * @brief Get the currently active version ID.
 * @param mgr Manager
 * @return Active version ID, or 0 if none deployed
 */
uint64_t cdsl_manager_get_active_version(const cdsl_manager_t* mgr);

/**
 * @brief Execute the currently active ruleset against a context.
 *
 * Convenience function that acquires the active snapshot, runs `cdsl_vm_execute_ruleset`,
 * and releases the snapshot cleanly.
 *
 * @param mgr Manager
 * @param vm VM instance
 * @param ctx Context
 * @return Ruleset evaluation batch report, or NULL on error
 */
cdsl_ruleset_report_t*
cdsl_manager_execute(cdsl_manager_t* mgr, cdsl_vm_t* vm, cdsl_context_t* ctx);

/**
 * @brief Get information about available historical snapshots.
 * @param mgr Manager
 * @param out_versions Array to receive version IDs (size at least max_count)
 * @param max_count Maximum number of version IDs to return
 * @return Number of versions returned
 */
size_t
cdsl_manager_get_history(const cdsl_manager_t* mgr, uint64_t* out_versions, size_t max_count);

struct cdsl_config_source;

/**
 * @brief Attach a distributed configuration source (Etcd/Consul/Memory) to the rule manager.
 *
 * Automatically fetches the initial rule configuration, executes canary pre-flight validation,
 * deploys the verified ruleset, and registers a real-time watch handler for zero-downtime updates.
 *
 * @param mgr Rule manager instance
 * @param src Configuration source driver
 * @param key Configuration key to fetch and watch
 * @return 0 on success, negative on error
 */
int cdsl_manager_attach_config_source(cdsl_manager_t* mgr,
				      struct cdsl_config_source* src,
				      const char* key);

#ifdef __cplusplus
}
#endif

#endif /* CDSL_MANAGER_H */
