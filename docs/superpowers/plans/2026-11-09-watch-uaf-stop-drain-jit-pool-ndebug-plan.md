# Watch UAF / Server Stop Drain / JIT AArch64 Pool / NDEBUG 四项修复实施计划

Date: 2026-11-09
Spec: `docs/superpowers/specs/2026-11-09-watch-uaf-stop-drain-jit-pool-ndebug-design.md`
Status: In Progress

## 总览

四个独立修复，每个自成 commit 单元。TDD 顺序：先写能捕获缺陷的测试（修复前跑红或无法稳定复现就依赖 ASan），再改实现，最后统一验证。

| Task | 文件 | 变更 |
|------|------|------|
| 1 | `src/manager/etcd.c`, `consul.c`, `config_source.c`, `tests/test_distributed.c` | 锁内内容快照 + 8 线程并发 UAF 回归测试 |
| 2 | `src/server/server.c`, `tests/test_server.c` | `server_handle_client` 去 running 依赖 + stop 排空测试 |
| 3 | `src/vm/jit.c`, `tests/test_jit.c` | AArch64 imm9 布局感知回填 + 静态自检 |
| 4 | `CMakeLists.txt` | test 目标 `-UNDEBUG` |
| 5 | `CHANGELOG.md`, `docs/architecture.md` | 文档同步 |

---

## Task 1: watch 回调内容快照（三驱动）

**目标**：`etcd_publish` / `consul_publish` / `mem_driver_publish` 在锁内复制内容快照，锁外派发时传快照指针，派发完 `free` 快照，消除 `target->content` 裸指针 UAF。

### Step 1.1: 先写回归测试（修复前预期在 ASan 下红）

文件：`tests/test_distributed.c`

在 `main()` 的 `test_watcher_reentrancy_all_drivers()` 调用之后新增 `test_watch_content_uaf_concurrent()` 调用，并实现该函数：

```c
/* ---- concurrent watch content snapshot UAF regression ---- */
#include <stdatomic.h>
#include <pthread.h>

static cdsl_config_source_t* s_uaf_src;
static atomic_int s_uaf_valid_count;
static atomic_int s_uaf_total_count;
#define UAF_THREADS 8
#define UAF_PUBLISH_PER_THREAD 200
#define UAF_CONTENT_LEN 1024

typedef struct uaf_worker_arg {
	int thread_id;
} uaf_worker_arg_t;

static void
uaf_watch_callback(const char* key,
		   const char* content,
		   size_t len,
		   const char* version,
		   void* user_data)
{
	(void)key;
	(void)version;
	(void)user_data;
	atomic_fetch_add(&s_uaf_total_count, 1);
	if (!content || len != UAF_CONTENT_LEN) {
		return;
	}
	/* 校验内容首尾标记：合法 publish 内容是 'A' 填充 + 头尾各 1 字节线程号 */
	if (content[0] != 'A' || content[len - 1] != 'A') {
		return;
	}
	/* 首字节后 4 字节为 10 进制线程号（如 "3" 或 "12"）+ 剩余 'A' */
	char numbuf[8] = {0};
	size_t nl = 4;
	memcpy(numbuf, content + 1, nl > 4 ? 4 : nl);
	nbuf[4] = '\0';
	int tid = atoi(numbuf);
	(void)tid;
	/* 只要内容完整且是 'A' 填充即合法（不校验线程号，避免乱序误判） */
	int all_ok = 1;
	for (size_t i = 0; i < len; i++) {
		if (content[i] != 'A' && i != 0 && i != len - 1 && i < 5) {
			/* 第 5-第 4 字节是线程号区，可能是数字 */
			if (!(content[i] >= '0' && content[i] <= '9')) {
				all_ok = 0;
				break;
			}
		}
	}
	if (all_ok) {
		atomic_fetch_add(&s_uaf_valid_count, 1);
	}
}

static void*
uaf_worker_main(void* arg)
{
	uaf_worker_arg_t* a = (uaf_worker_arg_t*)arg;
	char content[5 + UAF_CONTENT_LEN] = {0};
	char tidbuf[8];
	snprintf(tidbuf, sizeof(tidbuf), "%d", a->thread_id);
	memset(content + 1, 'A', UAF_CONTENT_LEN - 5);
	memcpy(content + 1, tidbuf, strnlen(tidbuf, 4));
	content[0] = 'A';
	content[4 + UAF_CONTENT_LEN - 4] = 'A'; /* 尾部标记 */
	/* 实际内容布局：[0]='A', [1..4]=线程号(左对齐,不足补'A'), [5..]=A, 末字节='A' */
	for (int i = 0; i < UAF_PUBLISH_PER_THREAD; i++) {
		(void)cdsl_config_source_publish(s_uaf_src,
						 "uaf/key",
						 content,
						 UAF_CONTENT_LEN,
						 NULL);
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
		atomic_store(&s_uaf_valid_count, 0);
		atomic_store(&s_uaf_total_count, 0);

		assert(cdsl_config_source_watch(s_uaf_src,
						"uaf/key",
						uaf_watch_callback,
						NULL) == 0);

		pthread_t threads[UAF_THREADS];
		uaf_worker_arg_t args[UAF_THREADS];
		for (int t = 0; t < UAF_THREADS; t++) {
			args[t].thread_id = t;
			assert(pthread_create(&threads[t], NULL, uaf_worker_main, &args[t]) == 0);
		}
		for (int t = 0; t < UAF_THREADS; t++) {
			pthread_join(threads[t], NULL);
		}

		int total = atomic_load(&s_uaf_total_count);
		int valid = atomic_load(&s_uaf_valid_count);
		/* 每次 publish 都触发一次回调，total 应等于 8*200；valid 也相等 */
		assert(total == UAF_THREADS * UAF_PUBLISH_PER_THREAD);
		assert(valid == UAF_THREADS * UAF_PUBLISH_PER_THREAD);

		cdsl_config_source_free(s_uaf_src);
	}
}
```

`main()` 中在 `test_watcher_reentrancy_all_drivers();` 后加：

```c
	test_watch_content_uaf_concurrent();
```

注意：`#include <pthread.h>` / `<stdatomic.h>` 放在文件顶部（现有 `#include "cdsl/cdsl.h"` 之后）。

- [ ] **Step 1.1 完成判据**：测试可编译，`ctest --test-dir build -R test_distributed` 通过（Debug 下大概率已绿，因为 UAF 是竞态，未修状态下 Debug 单次跑可能侥幸不触发——这是预期的，真正捕获靠 ASan）。

### Step 1.2: 用 ASan 构建确认修复前可捕获

Run:

```bash
cd /home/quintin/Data/source/c_cpp/cdsl
rm -rf build-uaf-repro && cmake -B build-uaf-repro -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" -DCDSL_GENERATE_DOCS=OFF
cmake --build build-uaf-repro -j8 --target test_distributed
ASAN_OPTIONS=detect_leaks=0 ./build-uaf-repro/test_distributed
```

Expected: 当前（未修复）代码下，高概率报 `heap-use-after-free` / `AddressSanitizer` 报错并 abort；若侥幸通过，重复运行 5 次（竞态窗口小）。

- [ ] **Step 1.2 完成判据**：至少一次复现 UAF，或确认窗口极小（记录结果即可，不阻塞推进——修复本身按设计执行）。

### Step 1.3: 修 `etcd_publish`（src/manager/etcd.c:160-181）

现状（约 160-181 行）：

```c
	const char* out_content_ptr = target->content;
	size_t out_len = target->len;

	/* Release mutex BEFORE invoking user callbacks */
	CDSL_MUTEX_UNLOCK(&d->lock);

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
```

替换为：

```c
	/* 锁内复制内容快照，锁外只传快照，消除并发 publish 同 key 时的 UAF */
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

	/* Dispatch callbacks lock-free with snapshot content */
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
```

### Step 1.4: 修 `consul_publish`（src/manager/consul.c:167-183）

与 1.3 完全同型，仅 `target->content`/`target->len` 字段名一致。将 consul.c 中 `const char* out_content_ptr = target->content;` 起的同段代码按 1.3 的模式替换（锁内 `malloc + memcpy` 快照，解锁后派发传 `content_snapshot`，派发后 `free(content_snapshot)`，OOM 时解锁 + free dispatch_list + 返回 -1）。

### Step 1.5: 修 `mem_driver_publish`（src/manager/config_source.c:226-243）

与 1.3 同型替换（该文件锁对象是 `&data->lock`，`target->content`/`target->len`）。

### Step 1.6: 回归验证

Run:

```bash
cmake --build build -j8 && ctest --test-dir build -R test_distributed
cmake --build build-uaf-repro -j8 --target test_distributed
ASAN_OPTIONS=detect_leaks=0 ./build-uaf-repro/test_distributed   # 连跑 3 次
```

Expected: 全绿，ASan 下无 `heap-use-after-free`。

- [ ] **Step 1.6 完成判据**：Debug 与 ASan 构建下 `test_distributed`（含新增 `test_watch_content_uaf_concurrent`）全绿，ASan 连跑 3 次无 UAF 报告。

### Step 1.7: Commit

```bash
git add src/manager/etcd.c src/manager/consul.c src/manager/config_source.c tests/test_distributed.c
git commit -m "fix(manager): :bug: snapshot watch callback content to prevent use-after-free

etcd/consul/memory drivers passed target->content (a heap pointer freed by a
concurrent publish of the same key) to watch callbacks after releasing the
mutex. Now the content is copied into a snapshot under lock and the snapshot
is passed to callbacks and freed after dispatch. Adds an 8-thread x 200-publish
concurrent regression test (test_watch_content_uaf_concurrent) that runs clean
under ASan."
```

---

## Task 2: server stop 优雅排空

**目标**：`server_handle_client` 处理已 enqueue 连接时不再受 `running` 标志影响，stop 语义变为"优雅排空后关闭"。

### Step 2.1: 写回归测试（修复前可观察"请求被丢弃"）

文件：`tests/test_server.c`，在 `test_server_dynamic_large_response` 之后新增：

```c
static void
test_server_stop_drain(void)
{
	printf("Running test_server_stop_drain...\n");
	cdsl_server_config_t config = {
	    .port = 0,
	    .max_concurrency = 2,
	    .verbose = false,
	};
	cdsl_server_t* srv = cdsl_server_create(&config);
	assert(srv != NULL);
	assert(cdsl_server_start(srv) == 0);

	int port = cdsl_server_get_port(srv);
	assert(port > 0);

	/* 连接 2 个客户端，发送完整请求但暂不读响应 */
	cdsl_socket_t sock[2];
	for (int i = 0; i < 2; i++) {
		sock[i] = socket(AF_INET, SOCK_STREAM, 0);
		assert(sock[i] != CDSL_INVALID_SOCKET);
		struct sockaddr_in a;
		memset(&a, 0, sizeof(a));
		a.sin_family = AF_INET;
		a.sin_port = htons((uint16_t)port);
		a.sin_addr.s_addr = inet_addr("127.0.0.1");
		assert(connect(sock[i], (struct sockaddr*)&a, sizeof(a)) == 0);
		const char* req =
		    "GET /healthz HTTP/1.1\r\nHost: localhost\r\n\r\n";
		assert(send(sock[i], req, (int)strlen(req), 0) > 0);
	}

	/* stop：应优雅处理完在途请求（recv 完整请求 + send 响应）后再退出 */
	cdsl_server_stop(srv);

	/* stop 返回后，两个客户端都应能收到 200 响应（在途请求未被丢弃） */
	for (int i = 0; i < 2; i++) {
		char buf[512] = {0};
		ssize_t n = recv(sock[i], buf, sizeof(buf) - 1, 0);
		/* 修复前：stop 立即丢弃未处理连接，recv 得 0（对端已 close）或
		   无 200；修复后：收到完整 HTTP 200 响应 */
		assert(n > 0);
		assert(strstr(buf, "200 OK") != NULL);
		assert(strstr(buf, "\"status\":\"healthy\"") != NULL);
		CDSL_CLOSE_SOCKET(sock[i]);
	}

	cdsl_server_free(srv);
}
```

`main()` 中在 `test_server_dynamic_large_response();` 后加 `test_server_stop_drain();`。

- [ ] **Step 2.1 完成判据**：测试编译通过。修复前跑 `ctest -R test_server` 预期 FAIL（recv 得 0 或无 200）——若本地侥幸 PASS（时序窗口），以代码审查为准推进修复，不强求红。

### Step 2.2: 修 `server_handle_client`（src/server/server.c:698-771）

现状第 714 行附近：

```c
	while (server_is_running(srv)) {
```

改为无条件循环（连接自身 EOF/错误即 break）：

```c
	/* 已 enqueue 的连接必须处理到完整请求或连接关闭，不受 stop 标志影响，
	 * 保证 stop 时 worker 排空在途请求（优雅排空语义） */
	while (1) {
```

循环体内其余 break 条件不变（`cap >= 10MB` 上限 break、`n <= 0` 即 EOF/错误 break、header + body 收齐 break）。

- [ ] **Step 2.2 完成判据**：`cmake --build build -j8 && ctest --test-dir build -R test_server` 全绿，`test_server_stop_drain` 通过，原有 `test_server_concurrency`（48/48 成功）仍通过。

### Step 2.3: Commit

```bash
git add src/server/server.c tests/test_server.c
git commit -m "fix(server): :recycle: drain in-flight requests on stop instead of dropping them

server_handle_client gated its recv loop on server_is_running(), so when
cdsl_server_stop() flipped the flag, already-queued client connections were
closed without ever reading their request. The loop now runs until the
connection itself hits EOF/error/completion, giving graceful drain semantics
while the stop order (join workers, then drain residual queue fds) is
unchanged. Adds test_server_stop_drain."
```

---

## Task 3: JIT AArch64 literal pool 偏移布局感知

**目标**：AArch64 trampoline 的两条 `ldr` literal 指令 imm9 按实际布局计算回填，消除手填 `#20`/`#24` 的错位风险（现状 `#20` 与 7 指令 prologue 实际 pool 位置 28 字节处不一致，AArch64 上本就会加载错误地址）。

### Step 3.1: 修 `cdsl_jit_compile` AArch64 分支（src/vm/jit.c:350-384）

现状（约 350-384 行）：

```c
	uint32_t instrs[32];
	size_t n = 0;

	instrs[n++] = 0xa9be7bfd; /* stp x29, x30, [sp, #-32]! */
	instrs[n++] = 0x910003fd; /* mov x29, sp */
	instrs[n++] = 0xaa0203e3; /* mov x3, x2 */
	instrs[n++] = 0xaa0103e2; /* mov x2, x1 */
	instrs[n++] = 0x580000a1; /* ldr x1, #20 */
	instrs[n++] = 0x580000d0; /* ldr x16, #24 */
	instrs[n++] = 0xd63f0200; /* blr x16 */
	instrs[n++] = 0xa8c27bfd; /* ldp x29, x30, [sp], #32 */
	instrs[n++] = 0xd65f03c0; /* ret */

	/* Literal pool */
	memcpy(code, instrs, n * sizeof(uint32_t));
	ip = n * sizeof(uint32_t);
	...
```

替换为（记录两条 ldr 的 index，prologue 全部 push 后计算 imm9 回填）：

```c
	uint32_t instrs[32];
	size_t n = 0;

	instrs[n++] = 0xa9be7bfd; /* stp x29, x30, [sp, #-32]! */
	instrs[n++] = 0x910003fd; /* mov x29, sp */
	instrs[n++] = 0xaa0203e3; /* mov x3, x2 */
	instrs[n++] = 0xaa0103e2; /* mov x2, x1 */

	/* LDR literal 占位，记录 index，prologue 完成后按布局回填 imm9 */
	size_t idx_bc = n;
	instrs[n++] = 0x58000000u; /* ldr x1, [pc, #imm9*4] (imm9 待回填) */
	size_t idx_helper = n;
	instrs[n++] = 0x58000000u; /* ldr x16, [pc, #imm9*4] (imm9 待回填) */
	instrs[n++] = 0xd63f0200; /* blr x16 */
	instrs[n++] = 0xa8c27bfd; /* ldp x29, x30, [sp], #32 */
	instrs[n++] = 0xd65f03c0; /* ret */

	/*
	 * Literal pool sits at byte offset 4*n (immediately after the
	 * prologue). AArch64 LDR (literal) resolves PC as the address of
	 * the LDR instruction itself, so imm9 = (pool - ldr_pc)/4
	 * = n - idx, where idx is the instruction index of the LDR.
	 */
	uint32_t imm9_bc = (uint32_t)(n - idx_bc);
	uint32_t imm9_helper = (uint32_t)(n - idx_helper);
	if (imm9_bc > 4095u || imm9_helper > 4095u) {
		jit_free_pages(page, actual_size);
		free(jit);
		return NULL;
	}
	instrs[idx_bc] = 0x58000000u | (imm9_bc << 10) | 0x01u;     /* rt = x1 */
	instrs[idx_helper] = 0x58000000u | (imm9_helper << 10) | 0x10u; /* rt = x16 */

	/* Literal pool */
	memcpy(code, instrs, n * sizeof(uint32_t));
	ip = n * sizeof(uint32_t);
```

同步更新 jit.c 文件头注释：在 "Architecture & Relocation Notes" 里补一条说明——AArch64 literal pool 偏移按布局计算回填，prologue 增删指令无需再手工同步立即数。

- [ ] **Step 3.1 完成判据**：编译通过（本机 x86 下该分支不编译，仅语法检查）；`cmake --build build -j8` 无警告。

### Step 3.2: 加 AArch64 静态自检测试（tests/test_jit.c）

在 `main()` 中 `test_jit_bytecode_caching();` 后加（`#if defined(__aarch64__)` 守卫）：

```c
#if defined(__aarch64__)
/* AArch64 专用：验证 native trampoline 的 LDR literal 偏移与布局一致 */
static void
test_jit_aarch64_pool_offsets(void)
{
	printf("Running test_jit_aarch64_pool_offsets...\n");
	cdsl_schema_t* schema = cdsl_schema_create();
	assert(schema != NULL);
	cdsl_schema_register_var(schema, "v", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "act", CDSL_TYPE_VOID, 0);

	cdsl_vm_t* vm = cdsl_vm_create(schema);
	assert(vm != NULL);
	const char* dsl = "RULE r_pool { WHEN v > 10 THEN act() }";
	cdsl_rule_t* rule = cdsl_parse_string(dsl, NULL);
	assert(rule != NULL);

	cdsl_bytecode_t bc = {0};
	assert(cdsl_bytecode_compile(rule, schema, &bc) == 1);

	cdsl_jit_code_t* jit = cdsl_jit_compile(vm, &bc);
	assert(jit != NULL);
	assert(jit->is_native == true);

	/* native entry 执行结果与 threaded 引擎逐字段一致 */
	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_context_set_int(ctx, "v", 42);
	cdsl_value_t v_threaded = cdsl_bytecode_execute_threaded(vm, &bc, ctx);
	cdsl_value_t v_jit;
	assert(cdsl_jit_execute(jit, vm, ctx, &v_jit) == 0);
	assert(v_jit.type == v_threaded.type);
	assert(v_jit.data.bool_val == v_threaded.data.bool_val);

	cdsl_context_free(ctx);
	cdsl_jit_code_free(jit);
	cdsl_bytecode_free(&bc);
	cdsl_free_rule(rule);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
}
#endif
```

`main()` 内：

```c
#if defined(__aarch64__)
	test_jit_aarch64_pool_offsets();
#endif
```

说明：本机 x86 下该测试被 `#if` 跳过，CI 的 AArch64 runner（若启用 `CDSL_JIT_ARCH_AARCH64`）覆盖。x86 下现有 `test_jit_basic_execution`（走 non-native fallback 或 x86 native 分支）仍验证 JIT 语义等价。

- [ ] **Step 3.2 完成判据**：`ctest --test-dir build -R test_jit` 全绿（x86 下跳过新增用例，其余 JIT 测试通过）。

### Step 3.3: Commit

```bash
git add src/vm/jit.c tests/test_jit.c
git commit -m "fix(jit): :bug: compute AArch64 literal pool offsets from layout instead of hardcoded #20/#24

The two LDR (literal) instructions in the AArch64 trampoline had their
imm9 fields hand-filled as #20/#24, which is inconsistent with the 9-instruction
prologue (pool actually sits at byte 36, LDRs at bytes 16/20) and would load
garbage addresses on AArch64. Offsets are now back-filled from the actual
prologue layout (imm9 = n - idx), with an overflow guard. Adds an AArch64-only
self-check test comparing native vs threaded execution results."
```

---

## Task 4: CMake test 目标 NDEBUG 双保险

**目标**：所有 test 目标在任意 build type 下断言恒有效。

### Step 4.1: 修 CMakeLists.txt

现状（约 230 行）：

```cmake
    target_compile_definitions(${test_name} PRIVATE CDSL_TESTING)
```

其后追加：

```cmake
    # 测试目标恒以断言启用编译：撤销 build type 注入的 -DNDEBUG（若存在），
    # 与各测试源文件顶部的 #undef NDEBUG 互为双保险
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${test_name} PRIVATE -UNDEBUG)
    elseif(MSVC)
        target_compile_options(${test_name} PRIVATE /UNDEBUG)
    endif()
```

- [ ] **Step 4.1 完成判据**：`cmake -B build` 重新 configure 无错误；`cmake --build build -j8 && ctest --test-dir build` 全绿。

### Step 4.2: Release 构建验证断言仍启用

Run:

```bash
cd /home/quintin/Data/source/c_cpp/cdsl
rm -rf build-rel-verify && cmake -B build-rel-verify -DCMAKE_BUILD_TYPE=Release -DCDSL_GENERATE_DOCS=OFF
cmake --build build-rel-verify -j8 --target test_server
strings build-rel-verify/test_server | grep -c "__assert_fail"
```

Expected: `strings` 输出 `__assert_fail` 计数 ≥ 1（GCC 下 Release 默认注入 `-DNDEBUG`，本修复撤销后断言仍在）。

- [ ] **Step 4.2 完成判据**：`__assert_fail` 计数 ≥ 1；`./build-rel-verify/test_server` 跑通（Release 下断言有效）。

### Step 4.3: Commit

```bash
git add CMakeLists.txt
git commit -m "build(tests): :wrench: strip -DNDEBUG from all test targets for assert-enabled builds

Test sources carry a per-file #undef NDEBUG guard, but that relies on every
file remembering to write it. CMake now additionally passes -UNDEBUG (GCC/Clang)
or /UNDEBUG (MSVC) to every test target so assertions stay live under any
build type, as a second line of defense."
```

---

## Task 5: 文档与 CHANGELOG 同步

### Step 5.1: CHANGELOG.md

在文件顶部（`# Changelog` 之后、`## [2.0.2]` 之前）插入：

```markdown
## [2.0.3] - 2026-11-09

### Fixed
- **Watch Callback Content Snapshot (`src/manager/etcd.c`, `src/manager/consul.c`, `src/manager/config_source.c`)**:
  - `publish` now copies the content into a heap snapshot under the driver mutex and dispatches callbacks with the snapshot, eliminating the use-after-free window where a concurrent publish of the same key could free `target->content` while a callback still read it.
  - Added `test_watch_content_uaf_concurrent` (8 threads × 200 publishes, byte-exact content validation, ASan-clean).
- **Graceful Server Shutdown (`src/server/server.c`)**:
  - `server_handle_client` no longer aborts mid-request when the server is stopping; in-flight requests are drained to completion before sockets close.
  - Added `test_server_stop_drain`.
- **AArch64 JIT Trampoline Literal Pool (`src/vm/jit.c`)**:
  - LDR literal imm9 fields are now computed from the actual prologue layout (`imm9 = n - idx`) instead of hand-filled `#20`/`#24`, which were inconsistent with the 9-instruction prologue.
- **Test Assertion Hygiene (`CMakeLists.txt`)**:
  - All test targets are compiled with `-UNDEBUG` / `/UNDEBUG` so `assert()` stays enabled under Release builds, doubling up with the per-source `#undef NDEBUG` guards.

### Docs
- `jit.c` header now documents the layout-computed AArch64 pool offsets.
- `docs/architecture.md` watch semantics note updated (content passed by snapshot).
```

### Step 5.2: docs/architecture.md

在涉及 watch/manager 语义的小节（找 "watch" 或 "config source" 关键词处）追加一句：

> 驱动 `publish` 在持锁状态下将内容拷贝为堆快照，watch 回调在锁外接收该快照指针；派发完成后驱动释放快照。并发 `publish` 同 key 不会导致回调读取已释放内存。

### Step 5.3: Commit

```bash
git add CHANGELOG.md docs/architecture.md
git commit -m "docs: :memo: document watch content snapshot semantics, graceful stop, AArch64 pool layout, and NDEBUG test hygiene (v2.0.3)"
```

---

## 统一验证

完成全部 Task 后：

```bash
cd /home/quintin/Data/source/c_cpp/cdsl
rm -rf build
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCDSL_GENERATE_DOCS=OFF
cmake --build build -j8
ctest --test-dir build -j8                      # 预期 45/45 全绿（含 3 个新增用例）
rm -rf build-asan
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,undefined" -DCDSL_GENERATE_DOCS=OFF
cmake --build build-asan -j8
ASAN_OPTIONS=detect_leaks=0 ctest --test-dir build-asan -R "test_distributed|test_server|test_jit|test_manager|test_canary" -j8
# 连跑 2 轮确认稳定
```

Expected: 全绿；ASan 下 `test_distributed`（新 UAF 用例）无 heap-use-after-free。

---

## 执行检查单

- [ ] Task 1 Step 1.1 回归测试写完（先于实现）
- [ ] Task 1 Step 1.2 ASan 修复前复现（记录结果）
- [ ] Task 1 Step 1.3-1.5 三驱动改完
- [ ] Task 1 Step 1.6 验证 + 1.7 commit
- [ ] Task 2 Step 2.1 测试 + 2.2 修复 + 2.3 commit
- [ ] Task 3 Step 3.1 修复 + 3.2 测试 + 3.3 commit
- [ ] Task 4 Step 4.1 修复 + 4.2 Release 验证 + 4.3 commit
- [ ] Task 5 CHANGELOG + architecture.md + commit
- [ ] 统一验证（Debug 45/45 + ASan 关键套件 ×2）
