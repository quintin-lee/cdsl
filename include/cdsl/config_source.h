/**
 * @file include/cdsl/config_source.h
 * @brief Distributed configuration driver interface (Etcd, Consul, Memory).
 *
 * Provides cluster configuration synchronization, watch events,
 * and automated canary validation for CDSL rule engines.
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
 * @brief Create an in-memory configuration source driver for zero-dependency local use and testing.
 * @return New configuration source instance, or NULL on error
 */
cdsl_config_source_t* cdsl_config_source_create_mem(void);

/**
 * @brief Create an Etcd v3 configuration source driver.
 * @param endpoints Comma-separated list of Etcd HTTP/gRPC endpoints (e.g. "http://127.0.0.1:2379")
 * @param key_prefix Key prefix namespace (e.g. "/cdsl/rules/")
 * @return New configuration source instance, or NULL on error
 */
cdsl_config_source_t* cdsl_config_source_create_etcd(const char* endpoints, const char* key_prefix);

/**
 * @brief Create a Consul KV configuration source driver.
 * @param endpoint Consul HTTP endpoint (e.g. "http://127.0.0.1:8500")
 * @param key_prefix Key prefix namespace (e.g. "cdsl/rules/")
 * @return New configuration source instance, or NULL on error
 */
cdsl_config_source_t* cdsl_config_source_create_consul(const char* endpoint,
						       const char* key_prefix);

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
