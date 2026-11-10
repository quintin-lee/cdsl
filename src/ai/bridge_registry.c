/**
 * @file src/ai/bridge_registry.c
 * @brief AI provider and cache driver registries (split from bridge.c).
 *
 * The registries are the only mutable global state in the AI layer.  Keeping
 * them in their own translation unit lets every other bridge_* source file
 * reach them through the shared internal header instead of each defining its
 * own copy, and lets tests link exactly one registry instance.
 *
 * NOTE (intentional leak): re-registering a name does NOT free the previous
 * entry.  Concurrent readers may hold a pointer obtained under the read lock,
 * so freeing it would race with those readers.  Registration is rare, the
 * struct is small, and the leak is negligible — but it does remove the
 * use-after-free window.
 */

#include "cdsl/ai.h"
#include "bridge_internal.h"

static cdsl_hashmap_t* g_providers = NULL;
static cdsl_hashmap_t* g_cache_drivers = NULL;
static cdsl_rwlock_t g_ai_registry_lock;
static cdsl_once_t g_ai_registry_once = CDSL_ONCE_INIT;

static void
init_registries(void)
{
	CDSL_RWLOCK_INIT(&g_ai_registry_lock);
	g_providers = cdsl_hashmap_create(16);
	g_cache_drivers = cdsl_hashmap_create(16);
}

void
cdsl_ai_register_provider(const char* name, const cdsl_ai_provider_t* provider)
{
	if (!name || !provider) {
		return;
	}
	CDSL_ONCE_RUN(&g_ai_registry_once, init_registries);
	CDSL_RWLOCK_WRLOCK(&g_ai_registry_lock);
	cdsl_ai_provider_t* copy = malloc(sizeof(*copy));
	if (copy) {
		*copy = *provider;
		cdsl_hashmap_put(g_providers, name, copy);
	}
	CDSL_RWLOCK_UNLOCK_WR(&g_ai_registry_lock);
}

void
cdsl_ai_register_cache_driver(const char* name, const cdsl_ai_cache_t* cache)
{
	if (!name || !cache) {
		return;
	}
	CDSL_ONCE_RUN(&g_ai_registry_once, init_registries);
	CDSL_RWLOCK_WRLOCK(&g_ai_registry_lock);
	cdsl_ai_cache_t* copy = malloc(sizeof(*copy));
	if (copy) {
		*copy = *cache;
		cdsl_hashmap_put(g_cache_drivers, name, copy);
	}
	CDSL_RWLOCK_UNLOCK_WR(&g_ai_registry_lock);
}

cdsl_ai_provider_t*
cdsl_ai_get_provider(const char* name)
{
	CDSL_ONCE_RUN(&g_ai_registry_once, init_registries);
	CDSL_RWLOCK_RDLOCK(&g_ai_registry_lock);
	cdsl_ai_provider_t* p =
	    (cdsl_ai_provider_t*)cdsl_hashmap_get(g_providers, name ? name : "default");
	CDSL_RWLOCK_UNLOCK_RD(&g_ai_registry_lock);
	return p;
}

cdsl_ai_cache_t*
cdsl_ai_get_cache_driver(const cdsl_ai_config_t* cfg)
{
	if (cfg->cache) {
		return cfg->cache;
	}
	if (cfg->cache_driver_name) {
		CDSL_ONCE_RUN(&g_ai_registry_once, init_registries);
		CDSL_RWLOCK_RDLOCK(&g_ai_registry_lock);
		cdsl_ai_cache_t* result =
		    (cdsl_ai_cache_t*)cdsl_hashmap_get(g_cache_drivers, cfg->cache_driver_name);
		CDSL_RWLOCK_UNLOCK_RD(&g_ai_registry_lock);
		return result;
	}
	return NULL;
}
