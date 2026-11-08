#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include "cdsl/cdsl.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void
test_sha256_known_vector(void)
{
	printf("Running test_sha256_known_vector...\n");
	char hex[65];

	/* SHA-256("") */
	cdsl_sha256_hash_string("", hex);
	assert(strcmp(hex, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855") ==
	       0);

	/* SHA-256("hello") */
	cdsl_sha256_hash_string("hello", hex);
	assert(strcmp(hex, "2cf24dba5fb0a30e26e83b2ac5b9e29e1b161e5c1fa7425e73043362938b9824") ==
	       0);
}

static void
test_ruleset_fingerprinting(void)
{
	printf("Running test_ruleset_fingerprinting...\n");
	const char* dsl1 = "RULE r { WHEN amount > 100 THEN accept() }";
	const char* dsl2 = "RULE r { WHEN amount > 100 THEN accept() }";
	const char* dsl3 = "RULE r { WHEN amount > 200 THEN accept() }";

	cdsl_rule_t* r1 = cdsl_parse_string(dsl1, NULL);
	cdsl_rule_t* r2 = cdsl_parse_string(dsl2, NULL);
	cdsl_rule_t* r3 = cdsl_parse_string(dsl3, NULL);
	assert(r1 && r2 && r3);

	char h1[65], h2[65], h3[65];
	assert(cdsl_rule_hash(r1, h1) == 0);
	assert(cdsl_rule_hash(r2, h2) == 0);
	assert(cdsl_rule_hash(r3, h3) == 0);

	/* Identical rules produce identical SHA-256 hash */
	assert(strcmp(h1, h2) == 0);
	/* Different threshold produces different hash */
	assert(strcmp(h1, h3) != 0);

	cdsl_ruleset_t* s1 = cdsl_ruleset_create();
	cdsl_ruleset_t* s2 = cdsl_ruleset_create();
	cdsl_ruleset_add(s1, r1, 10);
	cdsl_ruleset_add(s2, r2, 10);

	char sh1[65], sh2[65];
	assert(cdsl_ruleset_hash(s1, sh1) == 0);
	assert(cdsl_ruleset_hash(s2, sh2) == 0);
	assert(strcmp(sh1, sh2) == 0);

	cdsl_ruleset_free(s1);
	cdsl_ruleset_free(s2);
	cdsl_free_rule(r3);
}

static int s_watch_invoked = 0;
static void
test_watch_callback(
    const char* key, const char* content, size_t len, const char* version, void* user_data)
{
	(void)key;
	(void)content;
	(void)len;
	(void)version;
	int* flag = (int*)user_data;
	if (flag) {
		(*flag)++;
	}
	s_watch_invoked++;
}

static void
test_drivers_crud(void)
{
	printf("Running test_drivers_crud (Memory, Etcd, Consul in-memory)...\n");
	cdsl_config_source_t* sources[] = {
	    cdsl_config_source_create_mem(),
	    cdsl_config_source_create_etcd("http://127.0.0.1:2379", "/cdsl/rules/"),
	    cdsl_config_source_create_etcd_mem("/cdsl/rules/"),
	    cdsl_config_source_create_consul("http://127.0.0.1:8500", "cdsl/rules/"),
	    cdsl_config_source_create_consul_mem("cdsl/rules/"),
	};
	const char* expected_names[] = {
	    "memory",
	    "etcd-memory",
	    "etcd-memory",
	    "consul-memory",
	    "consul-memory",
	};

	size_t num_sources = sizeof(sources) / sizeof(sources[0]);
	for (size_t i = 0; i < num_sources; i++) {
		cdsl_config_source_t* src = sources[i];
		assert(src != NULL);
		assert(strcmp(cdsl_config_source_get_name(src), expected_names[i]) == 0);

		const char* key = "rules/finance";
		int watch_flag = 0;
		assert(cdsl_config_source_watch(src, key, test_watch_callback, &watch_flag) == 0);

		const char* content1 = "RULE finance { WHEN balance >= 1000 THEN approve() }";
		char* ver1 = NULL;

		assert(cdsl_config_source_publish(src, key, content1, strlen(content1), &ver1) ==
		       0);
		assert(ver1 != NULL);
		assert(watch_flag == 1);

		if (strstr(expected_names[i], "etcd") != NULL) {
			assert(strncmp(ver1, "etcd-rev-", 9) == 0);
		} else if (strstr(expected_names[i], "consul") != NULL) {
			assert(strncmp(ver1, "consul-idx-", 11) == 0);
		}

		char* fetched = NULL;
		size_t fetched_len = 0;
		char* ver2 = NULL;
		assert(cdsl_config_source_fetch(src, key, &fetched, &fetched_len, &ver2) == 0);
		assert(fetched != NULL);
		assert(strcmp(fetched, content1) == 0);
		assert(fetched_len == strlen(content1));
		assert(strcmp(ver1, ver2) == 0);

		free(ver1);
		free(ver2);
		free(fetched);

		cdsl_config_source_free(src);
	}
}

static void
test_canary_auto_reload_and_rollback(void)
{
	printf("Running test_canary_auto_reload_and_rollback...\n");
	cdsl_config_source_t* src = cdsl_config_source_create_mem();
	assert(src != NULL);

	cdsl_manager_t* mgr = cdsl_manager_create(NULL);
	assert(mgr != NULL);

	const char* key = "cluster_rules";
	const char* dsl_v1 = "RULE r { WHEN score > 50 THEN pass() }";

	/* 1. Initial publish */
	assert(cdsl_config_source_publish(src, key, dsl_v1, strlen(dsl_v1), NULL) == 0);

	/* 2. Attach config source to manager */
	assert(cdsl_manager_attach_config_source(mgr, src, key) == 0);

	cdsl_vm_t* vm = cdsl_vm_create(NULL);
	cdsl_context_t* ctx = cdsl_context_create(NULL);
	cdsl_context_set_int(ctx, "score", 80);

	/* 1. v1: score 80 triggers score > 50 -> total_failed == 1 */
	cdsl_ruleset_report_t* rpt1 = cdsl_manager_execute(mgr, vm, ctx);
	assert(rpt1 != NULL);
	assert(rpt1->total_failed == 1);
	cdsl_ruleset_report_free(rpt1);

	/* 3. Hot-reload valid v2: score 80 does not trigger score > 90 -> total_passed == 1 */
	const char* dsl_v2 = "RULE r { WHEN score > 90 THEN pass() }";
	assert(cdsl_config_source_publish(src, key, dsl_v2, strlen(dsl_v2), NULL) == 0);

	cdsl_ruleset_report_t* rpt2 = cdsl_manager_execute(mgr, vm, ctx);
	assert(rpt2 != NULL);
	assert(rpt2->total_passed == 1);
	cdsl_ruleset_report_free(rpt2);

	/* 4. Malformed update v3 (syntax error): canary rejects it and preserves v2 */
	const char* dsl_v3_bad = "RULE r { WHEN score >> 100 SYNTAX_ERROR }";
	assert(cdsl_config_source_publish(src, key, dsl_v3_bad, strlen(dsl_v3_bad), NULL) == 0);

	/* Active version remains v2 */
	cdsl_ruleset_report_t* rpt3 = cdsl_manager_execute(mgr, vm, ctx);
	assert(rpt3 != NULL);
	assert(rpt3->total_passed == 1);
	cdsl_ruleset_report_free(rpt3);

	/* 5. Rollback to version 1 */
	assert(cdsl_manager_rollback(mgr, 1) == 1);
	cdsl_ruleset_report_t* rpt4 = cdsl_manager_execute(mgr, vm, ctx);
	assert(rpt4 != NULL);
	assert(rpt4->total_failed == 1);
	cdsl_ruleset_report_free(rpt4);

	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_manager_free(mgr);
	cdsl_config_source_free(src);
}

static int s_reentrant_fetch_success = 0;
static int s_reentrant_publish_success = 0;

static void
test_reentrant_watch_callback(
    const char* key, const char* content, size_t len, const char* version, void* user_data)
{
	(void)content;
	(void)len;
	(void)version;
	cdsl_config_source_t* src = (cdsl_config_source_t*)user_data;
	if (!src) {
		return;
	}

	/* Reentrant fetch while in callback */
	char* fetched = NULL;
	size_t fetched_len = 0;
	char* fetched_ver = NULL;
	if (cdsl_config_source_fetch(src, key, &fetched, &fetched_len, &fetched_ver) == 0) {
		s_reentrant_fetch_success++;
		free(fetched);
		free(fetched_ver);
	}

	/* Reentrant publish to another key */
	const char* audit_key = "rules/audit";
	const char* audit_content = "RULE audit { WHEN true THEN log() }";
	if (cdsl_config_source_publish(
		src, audit_key, audit_content, strlen(audit_content), NULL) == 0) {
		s_reentrant_publish_success++;
	}
}

static void
test_watcher_reentrancy_all_drivers(void)
{
	printf("Running test_watcher_reentrancy_all_drivers...\n");
	cdsl_config_source_t* sources[] = {
	    cdsl_config_source_create_mem(),
	    cdsl_config_source_create_etcd_mem("/cdsl/rules/"),
	    cdsl_config_source_create_consul_mem("cdsl/rules/"),
	};

	size_t count = sizeof(sources) / sizeof(sources[0]);
	for (size_t i = 0; i < count; i++) {
		cdsl_config_source_t* src = sources[i];
		assert(src != NULL);

		const char* key = "rules/finance";
		assert(cdsl_config_source_watch(src, key, test_reentrant_watch_callback, src) == 0);

		int initial_fetch = s_reentrant_fetch_success;
		int initial_pub = s_reentrant_publish_success;

		const char* content = "RULE finance { WHEN amount > 1000 THEN flag() }";
		assert(cdsl_config_source_publish(src, key, content, strlen(content), NULL) == 0);

		assert(s_reentrant_fetch_success == initial_fetch + 1);
		assert(s_reentrant_publish_success == initial_pub + 1);

		cdsl_config_source_free(src);
	}
}

/* ---- concurrent watch content snapshot UAF regression ---- */
#include <stdatomic.h>
#include <pthread.h>

typedef struct uaf_worker_arg {
	int thread_id;
} uaf_worker_arg_t;

#define UAF_THREADS 8
#define UAF_PUBLISH_PER_THREAD 200
#define UAF_CONTENT_LEN 1024

static cdsl_config_source_t* s_uaf_src = NULL;
static atomic_int s_uaf_total_count = 0;
static atomic_int s_uaf_valid_count = 0;

/* 合法内容布局：'A' 填充，头部第 0 字节固定 'A'，第 5..8 字节内嵌线程号（左对齐、
 * 不足 4 位补 'A'），尾部最后一字节 'A'。回调据此逐字节校验快照完整性。 */
static void
uaf_watch_callback(
    const char* key, const char* content, size_t len, const char* version, void* user_data)
{
	(void)key;
	(void)version;
	(void)user_data;
	atomic_fetch_add_explicit(&s_uaf_total_count, 1, memory_order_relaxed);
	if (!content || len != UAF_CONTENT_LEN) {
		return;
	}
	for (size_t i = 0; i < len; i++) {
		int is_thread_num_slot = (i >= 5 && i <= 8);
		char c = content[i];
		if (c == 'A') {
			continue;
		}
		if (is_thread_num_slot && c >= '0' && c <= '9') {
			continue;
		}
		return; /* 内容被踩坏（UAF/竞争）→ 不计入 valid */
	}
	atomic_fetch_add_explicit(&s_uaf_valid_count, 1, memory_order_relaxed);
}

static void*
uaf_worker_main(void* arg)
{
	uaf_worker_arg_t* a = (uaf_worker_arg_t*)arg;
	char content[UAF_CONTENT_LEN];
	char tidbuf[5] = {0};
	int tb_len = snprintf(tidbuf, sizeof(tidbuf), "%d", a->thread_id);

	memset(content, 'A', sizeof(content));
	memcpy(content + 5, tidbuf, (size_t)(tb_len > 0 ? tb_len : 0));
	/* content[0] 与 content[len-1] 保持 'A' 标记 */

	for (int i = 0; i < UAF_PUBLISH_PER_THREAD; i++) {
		(void)cdsl_config_source_publish(
		    s_uaf_src, "uaf/key", content, UAF_CONTENT_LEN, NULL);
	}
	return NULL;
}

static void
test_watch_content_uaf_concurrent(void)
{
	printf("Running test_watch_content_uaf_concurrent...\n");
	cdsl_config_source_t* sources[] = {
	    cdsl_config_source_create_mem(),
	    cdsl_config_source_create_etcd_mem("/cdsl/rules/"),
	    cdsl_config_source_create_consul_mem("cdsl/rules/"),
	};

	size_t count = sizeof(sources) / sizeof(sources[0]);
	for (size_t i = 0; i < count; i++) {
		s_uaf_src = sources[i];
		assert(s_uaf_src != NULL);
		atomic_store_explicit(&s_uaf_total_count, 0, memory_order_relaxed);
		atomic_store_explicit(&s_uaf_valid_count, 0, memory_order_relaxed);

		assert(cdsl_config_source_watch(s_uaf_src, "uaf/key", uaf_watch_callback, NULL) ==
		       0);

		pthread_t threads[UAF_THREADS];
		uaf_worker_arg_t args[UAF_THREADS];
		for (int t = 0; t < UAF_THREADS; t++) {
			args[t].thread_id = t;
			assert(pthread_create(&threads[t], NULL, uaf_worker_main, &args[t]) == 0);
		}
		for (int t = 0; t < UAF_THREADS; t++) {
			pthread_join(threads[t], NULL);
		}

		int total = atomic_load_explicit(&s_uaf_total_count, memory_order_relaxed);
		int valid = atomic_load_explicit(&s_uaf_valid_count, memory_order_relaxed);
		assert(total == UAF_THREADS * UAF_PUBLISH_PER_THREAD);
		assert(valid == UAF_THREADS * UAF_PUBLISH_PER_THREAD);

		cdsl_config_source_free(s_uaf_src);
	}
}

int
main(void)
{
	test_sha256_known_vector();
	test_ruleset_fingerprinting();
	test_drivers_crud();
	test_watcher_reentrancy_all_drivers();
	test_canary_auto_reload_and_rollback();
	test_watch_content_uaf_concurrent();
	printf("All distributed configuration tests passed successfully!\n");
	return 0;
}
