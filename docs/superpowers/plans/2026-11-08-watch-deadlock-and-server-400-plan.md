# Watch Deadlock Elimination, Server Explicit 400 Rejection, and JIT Trampoline Refinement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate watch callback deadlock paths in all config source drivers using a snapshot-and-unlock pattern, replace server silent approval fallbacks with explicit 400 Bad Request rejections, and refine JIT documentation to reflect the Native Trampoline architecture.

**Architecture:** Distributed configuration drivers snapshot matching callbacks and release their internal mutex before dispatching watch events. The cloud-native HTTP server validates request payloads strictly, returning `400 Bad Request` on empty or malformed rule definitions instead of synthesizing `approve()` rules. The JIT layer and documentation are standardized on Native Trampoline & Direct Threading semantics.

**Tech Stack:** C23, POSIX Threads / Windows Critical Sections, CMake, CTest, GitHub Actions.

---

## File Structure

- **Modified Files**:
  - `src/manager/config_source.c`: In-memory driver snapshot-and-unlock callback dispatch.
  - `src/manager/etcd.c`: Etcd in-memory simulation driver snapshot-and-unlock callback dispatch.
  - `src/manager/consul.c`: Consul in-memory simulation driver snapshot-and-unlock callback dispatch.
  - `src/server/server.c`: HTTP request validation, removal of `default_eval` / `default_rule` synthesis, explicit `400 Bad Request` error responses.
  - `include/cdsl/jit.h`: Docstring clarification framing execution as Native Trampoline & Direct Threading.
  - `src/vm/jit.c`: Trampoline dispatch layer file comments clarification.
  - `README.md`: Feature list and architecture description alignment.
  - `CHANGELOG.md`: Release notes update for v2.0.1.
- **Test Files**:
  - `tests/test_distributed.c`: Self-reentrancy and cross-thread lock safety unit tests.
  - `tests/test_server.c`: Negative test cases asserting `400 Bad Request` on empty or invalid evaluate/deploy requests.

---

### Task 1: Concurrency-Safe Watch Callback Dispatch in Memory, Etcd, and Consul Drivers

**Files:**
- Modify: `src/manager/config_source.c:185-205`
- Modify: `src/manager/etcd.c:135-148`
- Modify: `src/manager/consul.c:135-148`
- Test: `tests/test_distributed.c`

- [x] **Step 1: Write the failing reentrancy test in `tests/test_distributed.c`**

Add `test_watcher_reentrancy()` to `tests/test_distributed.c`:
```c
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
	if (cdsl_config_source_publish(src, audit_key, audit_content, strlen(audit_content), NULL) == 0) {
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
```
And add call to `test_watcher_reentrancy_all_drivers()` in `main()`.

- [x] **Step 2: Run test to verify it deadlocks/fails**

Run: `cmake --build build -j$(nproc) && ./build/test_distributed`  
Expected: Process hangs/times out due to self-deadlock on `d->lock`.

- [x] **Step 3: Implement snapshot-and-unlock in `src/manager/config_source.c`, `src/manager/etcd.c`, and `src/manager/consul.c`**

In each driver's `publish()` function, define:
```c
typedef struct watcher_cb_entry {
	cdsl_config_watch_fn callback;
	void* user_data;
} watcher_cb_entry_t;
```
Replace the watcher iteration under lock with:
```c
	/* Snapshot matching watchers under lock */
	size_t match_count = 0;
	for (etcd_watcher_t* w = d->watchers; w; w = w->next) {
		if (strcmp(w->key, key) == 0 || strcmp(w->key, "*") == 0) {
			match_count++;
		}
	}

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
	for (etcd_watcher_t* w = d->watchers; w && idx < match_count; w = w->next) {
		if (strcmp(w->key, key) == 0 || strcmp(w->key, "*") == 0) {
			dispatch_list[idx].callback = w->callback;
			dispatch_list[idx].user_data = w->user_data;
			idx++;
		}
	}

	/* Prepare immutable references before unlocking */
	const char* out_content_ptr = target->content;
	size_t out_len = target->len;

	/* Release mutex BEFORE invoking user callbacks */
	CDSL_MUTEX_UNLOCK(&d->lock);

	/* Dispatch callbacks lock-free */
	for (size_t i = 0; i < idx; i++) {
		if (dispatch_list[i].callback) {
			dispatch_list[i].callback(key, out_content_ptr, out_len, vbuf, dispatch_list[i].user_data);
		}
	}

	if (dispatch_list != stack_entries) {
		free(dispatch_list);
	}

	return 0;
```
(Apply symmetrically to `mem_driver_publish` in `src/manager/config_source.c`, `etcd_publish` in `src/manager/etcd.c`, and `consul_publish` in `src/manager/consul.c`.)

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build -j$(nproc) && ./build/test_distributed`  
Expected: `All distributed configuration tests passed successfully!` with 0 hangs.

- [x] **Step 5: Commit**

```bash
git add src/manager/config_source.c src/manager/etcd.c src/manager/consul.c tests/test_distributed.c
git commit -m "fix(manager): :bug: eliminate watch callback deadlocks with snapshot-and-unlock pattern"
```

---

### Task 2: Server Explicit 400 Bad Request & Elimination of Default Approve Fallback

**Files:**
- Modify: `src/server/server.c:190-535`
- Test: `tests/test_server.c`

- [x] **Step 1: Write failing negative tests in `tests/test_server.c`**

Add negative test assertions in `tests/test_server.c`:
```c
	/* Negative test 1: POST /v1/evaluate with empty body when no rule deployed */
	char* resp_empty = cdsl_server_handle_request(srv, "POST /v1/evaluate HTTP/1.1\r\n\r\n");
	assert(resp_empty != NULL);
	assert(strstr(resp_empty, "400 Bad Request") != NULL);
	assert(strstr(resp_empty, "\"bad_request\"") != NULL);
	free(resp_empty);

	/* Negative test 2: POST /v1/evaluate with invalid syntax */
	const char* malformed_eval = "POST /v1/evaluate HTTP/1.1\r\n\r\n"
	                             "RULE bad { WHEN >>> 100 }";
	char* resp_malformed = cdsl_server_handle_request(srv, malformed_eval);
	assert(resp_malformed != NULL);
	assert(strstr(resp_malformed, "400 Bad Request") != NULL);
	free(resp_malformed);

	/* Negative test 3: POST /v1/rules/deploy with empty payload */
	char* resp_empty_deploy = cdsl_server_handle_request(srv, "POST /v1/rules/deploy HTTP/1.1\r\n\r\n{}");
	assert(resp_empty_deploy != NULL);
	assert(strstr(resp_empty_deploy, "400 Bad Request") != NULL);
	assert(strstr(resp_empty_deploy, "\"rejected\"") != NULL);
	free(resp_empty_deploy);
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build -j$(nproc) && ./build/test_server`  
Expected: FAIL on `assert(strstr(resp_empty, "400 Bad Request") != NULL)` (currently returns 200 OK).

- [x] **Step 3: Refactor `src/server/server.c`**

1. Introduce helper:
```c
static char*
server_http_json_response(int status_code, const char* status_text, const char* json_body)
{
	size_t body_len = json_body ? strlen(json_body) : 0;
	size_t cap = body_len + 256;
	char* resp = malloc(cap);
	if (!resp) {
		return NULL;
	}
	snprintf(resp, cap,
	    "HTTP/1.1 %d %s\r\n"
	    "Content-Type: application/json\r\n"
	    "Content-Length: %zu\r\n"
	    "Connection: close\r\n\r\n%s",
	    status_code, status_text, body_len, json_body ? json_body : "");
	return resp;
}
```

2. In `/v1/evaluate`:
- Remove `rule_dsl = "RULE default_eval { WHEN true THEN approve() }";`.
- If no DSL found:
  - Check `cdsl_manager_get_active_version(srv->mgr)`. If active, evaluate against `srv->mgr` ruleset.
  - If no active ruleset, return `server_http_json_response(400, "Bad Request", "{\"error\":\"bad_request\",\"message\":\"missing rule definition or evaluation target\"}\n")`.
- If parse fails for `dtable`, `scorecard`, `pipeline`, or `rule`:
  - Return `server_http_json_response(400, "Bad Request", "{\"error\":\"parse_error\",\"message\":\"failed to parse rule definition\"}\n")`.
- If VM execution returns NULL:
  - Return `server_http_json_response(500, "Internal Server Error", "{\"error\":\"evaluation_failed\"}\n")`.
- On success:
  - Return `server_http_json_response(200, "OK", json_result)`.

3. In `/v1/rules/deploy`:
- Remove `dsl_text = "RULE default_rule { WHEN true THEN approve() }";`.
- If `!dsl_text || strlen(dsl_text) == 0`:
  - Return `server_http_json_response(400, "Bad Request", "{\"status\":\"rejected\",\"error\":\"missing rule dsl to deploy\"}\n")`.

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build -j$(nproc) && ./build/test_server`  
Expected: PASS (all endpoints and negative test cases passing).

- [x] **Step 5: Commit**

```bash
git add src/server/server.c tests/test_server.c
git commit -m "fix(server): :security: replace silent approval fallback with explicit 400 Bad Request"
```

---

### Task 3: Refine JIT Trampoline Terminology in Headers, Code & Documentation

**Files:**
- Modify: `include/cdsl/jit.h`
- Modify: `src/vm/jit.c:1-10`
- Modify: `README.md`
- Modify: `CHANGELOG.md`

- [x] **Step 1: Update comments and docstrings in `include/cdsl/jit.h` and `src/vm/jit.c`**

Align terminology:
- Describe `cdsl_jit_compile()` as generating a native executable trampoline thunk bridging the host ABI to VM execution with W^X page protection and instruction cache synchronization.

- [x] **Step 2: Update `CHANGELOG.md` and `README.md`**

In `README.md`:
- Under Features -> Advanced Capabilities:
  - "Native Machine Code Trampoline — Architecture-specific W^X executable trampoline compiler for x86-64 and AArch64 fast-path execution"

In `CHANGELOG.md`:
- Clarify v2.0.0 entry:
  - "Native Machine Code Trampoline Fast-Path (`include/cdsl/jit.h`, `src/vm/jit.c`): Pure C native trampoline generation compiling ABI bridges to native instructions for x86-64 and AArch64 with W^X memory protection and instruction cache flush."
- Add v2.0.1 entry documenting:
  - Concurrency fix: snapshot-and-unlock in config sources eliminating callback deadlocks.
  - Server fix: explicit 400 Bad Request replacing default approve fallbacks.

- [x] **Step 3: Run test suite to verify no regressions**

Run: `ctest --test-dir build --output-on-failure`  
Expected: 100% tests passed out of 45.

- [x] **Step 4: Commit**

```bash
git add include/cdsl/jit.h src/vm/jit.c README.md CHANGELOG.md
git commit -m "docs(jit): :pencil2: accurately refine JIT terminology to Native Trampoline fast-path"
```

---

### Task 4: Local Regression & Full GitHub Actions CI Verification

**Files:**
- Full repository verification.

- [x] **Step 1: Check formatting**

Run: `clang-format --dry-run --Werror src/manager/*.c src/server/*.c tests/*.c`  
Expected: Exit code 0.

- [x] **Step 2: Run all 45 unit tests locally**

Run: `ctest --test-dir build --output-on-failure`  
Expected: 45/45 Passed.

- [x] **Step 3: Push to `origin master`**

Run: `git push origin master`

- [x] **Step 4: Monitor GitHub Actions CI Run**

Run: `gh run watch <run_id>`  
Expected: 13/13 Jobs Green.
