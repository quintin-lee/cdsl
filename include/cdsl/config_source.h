/**
 * @file include/cdsl/config_source.h
 * @brief In-memory configuration driver interface simulating Etcd and Consul data models.
 *
 * Provides thread-safe configuration synchronization, revision tracking, watch events,
 * and automated canary validation for CDSL rule engines with zero external network dependencies.
 */

#ifndef CDSL_CONFIG_SOURCE_H
#define CDSL_CONFIG_SOURCE_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cdsl_config_source cdsl_config_source_t;

/**
 * @brief Callback invoked when a watched configuration key changes.
 * @param key The configuration key that changed
 * @param content The new rule DSL content
 * @param len Length of the content
 * @param version Cluster version identifier or revision
 * @param user_data User-provided context
 */
typedef void (*cdsl_config_watch_fn)(
    const char* key, const char* content, size_t len, const char* version, void* user_data);

/**
 * @brief Vtable operations for configuration source drivers.
 */
typedef struct cdsl_config_source_ops {
	const char* (*name)(cdsl_config_source_t* src);
	int (*fetch)(cdsl_config_source_t* src,
		     const char* key,
		     char** out_content,
		     size_t* out_len,
		     char** out_version);
	int (*publish)(cdsl_config_source_t* src,
		       const char* key,
		       const char* content,
		       size_t len,
		       char** out_version);
	int (*watch)(cdsl_config_source_t* src,
		     const char* key,
		     cdsl_config_watch_fn callback,
		     void* user_data);
	void (*close)(cdsl_config_source_t* src);
} cdsl_config_source_ops_t;

struct cdsl_config_source {
	const cdsl_config_source_ops_t* ops;
	void* driver_data;
};

/**
 * @brief Create a generic in-memory configuration source driver for zero-dependency local use.
 * @return New configuration source instance, or NULL on error
 */
cdsl_config_source_t* cdsl_config_source_create_mem(void);

/**
 * @brief Create an in-memory Etcd v3 configuration source driver.
 *
 * Simulates Etcd v3 key-value semantics purely in-memory: maintains key namespaces,
 * generates monotonic revision IDs ("etcd-rev-<N>"), and delivers real-time notifications
 * to registered watchers.
 *
 * @param key_prefix Key prefix namespace (defaults to "/cdsl/rules/" if NULL)
 * @return New configuration source instance, or NULL on error
 */
cdsl_config_source_t* cdsl_config_source_create_etcd_mem(const char* key_prefix);

/**
 * @brief Create an in-memory Consul KV configuration source driver.
 *
 * Simulates Consul KV semantics purely in-memory: maintains key namespaces,
 * generates monotonic ModifyIndex identifiers ("consul-idx-<N>"), and delivers real-time
 * notifications to registered watchers.
 *
 * @param key_prefix Key prefix namespace (defaults to "cdsl/rules/" if NULL)
 * @return New configuration source instance, or NULL on error
 */
cdsl_config_source_t* cdsl_config_source_create_consul_mem(const char* key_prefix);

/**
 * @brief Create an in-memory Etcd v3 configuration source driver (compatibility alias).
 *
 * Provided for backward compatibility. Endpoint parameters are retained for API compatibility,
 * while underlying storage operates as an in-memory revision-tracked source.
 *
 * @param endpoints Endpoint string (retained for compatibility, defaults to "http://127.0.0.1:2379")
 * @param key_prefix Key prefix namespace (defaults to "/cdsl/rules/" if NULL)
 * @return New configuration source instance, or NULL on error
 */
cdsl_config_source_t* cdsl_config_source_create_etcd(const char* endpoints, const char* key_prefix);

/**
 * @brief Create an in-memory Consul KV configuration source driver (compatibility alias).
 *
 * Provided for backward compatibility. Endpoint parameters are retained for API compatibility,
 * while underlying storage operates as an in-memory ModifyIndex-tracked source.
 *
 * @param endpoint Endpoint string (retained for compatibility, defaults to "http://127.0.0.1:8500")
 * @param key_prefix Key prefix namespace (defaults to "cdsl/rules/" if NULL)
 * @return New configuration source instance, or NULL on error
 */
cdsl_config_source_t* cdsl_config_source_create_consul(const char* endpoint,
						       const char* key_prefix);

/**
 * @brief Get the driver name of a configuration source (e.g. "memory", "etcd-memory", "consul-memory").
 * @param src Configuration source instance
 * @return Non-null driver name string, or "unknown" if src/ops is NULL
 */
const char* cdsl_config_source_get_name(const cdsl_config_source_t* src);

/**
 * @brief Fetch configuration by key.
 */
int cdsl_config_source_fetch(cdsl_config_source_t* src,
			     const char* key,
			     char** out_content,
			     size_t* out_len,
			     char** out_version);

/**
 * @brief Publish or update configuration by key.
 */
int cdsl_config_source_publish(cdsl_config_source_t* src,
			       const char* key,
			       const char* content,
			       size_t len,
			       char** out_version);

/**
 * @brief Watch a configuration key for changes.
 */
int cdsl_config_source_watch(cdsl_config_source_t* src,
			     const char* key,
			     cdsl_config_watch_fn callback,
			     void* user_data);

/**
 * @brief Free configuration source driver.
 */
void cdsl_config_source_free(cdsl_config_source_t* src);

#ifdef __cplusplus
}
#endif

#endif /* CDSL_CONFIG_SOURCE_H */
