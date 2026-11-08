# Design Spec: Watch UAF、Server Stop Drain、JIT AArch64 Pool Offset、NDEBUG 空转四项修复

Date: 2026-11-09
Status: Proposed
Owner: Quintin
Scope: `src/manager/{etcd,consul,config_source}.c`, `src/server/server.c`, `src/vm/jit.c`, `tests/test_distributed.c`, `tests/test_server.c`, `CMakeLists.txt`

## 1. 背景与目标

v2.0.x 系列完成了 watch 死锁消除（snapshot-and-unlock）、server 显式 400 拒绝、JIT 术语澄清、server 线程池等收敛。代码评审仍遗留四个功能性缺陷，本 spec 一次性修复：

| # | 缺陷 | 严重度 | 位置 |
|---|------|--------|------|
| 1 | watch 回调传裸指针 `target->content`，并发 publish 同 key 时 UAF | 高 | etcd.c:168-180, consul.c:169-181, config_source.c:228-237 |
| 2 | `cdsl_server_stop` drain 残留 socket 的时序，与 worker 处理窗口竞争 | 中 | server.c:948-981 |
| 3 | JIT AArch64 literal pool 偏移 `#20`/`#24` 手工算死，prologue 变化即静默错位 | 中 | jit.c:368-384 |
| 4 | `tests/test_server.c` 依赖 `assert` 宏，Release/`-DNDEBUG` 构建下测试体空转 | 中 | test_server.c:1-3, CMakeLists.txt:217-237 |

**目标**：① 三驱动 watch 派发语义统一为"内容快照"，消除 UAF；② stop 时序消除 worker/drain 竞争；③ AArch64 偏移改为布局感知计算；④ 所有 test 目标在任意 build type 下断言恒有效。**非目标**：不实现 etcd/consul 真客户端（保持 in-memory 驱动定位，文件头注释已如实标注），不实现 JIT 逐指令发射（v2.1.0 另立 spec）。

## 2. 修复 ①：watch 回调内容快照（三驱动同改）

### 缺陷分析

以 `etcd_publish` 为例：锁内完成 `target->content = malloc(len+1)` 赋值后，记录 `out_content_ptr = target->content`（etcd.c:168），**解锁后**（etcd.c:173）才在 177-180 把 `out_content_ptr` 传给回调。窗口内另一线程 `publish` 同 key 会在 etcd.c:118 `free(target->content)`，回调读到已释放内存。`consul_publish`（consul.c:169-181）与 `mem_driver_publish`（config_source.c:228-237，`out_content_ptr = target->content`）同型。

下游回调 `manager_config_source_cb`（manager.c:319-332）只在回调内**同步**使用 `content`（传给 `cdsl_manager_deploy_canary`，后者内部拷贝），不存留指针——因此快照语义安全，无需回调方改动。

### 改法

三个驱动的 `publish` 统一为：**锁内 malloc 一份内容快照并 `memcpy`；解锁后把快照传给回调；派发循环结束后 `free` 快照**。

```c
/* etcd.c / consul.c / config_source.c 同一模式 */
char* content_snapshot = malloc(len + 1);
if (!content_snapshot) {
    CDSL_MUTEX_UNLOCK(&d->lock);
    return -1;
}
memcpy(content_snapshot, target->content, len);
content_snapshot[len] = '\0';

/* 解锁前不再保留 target->content 裸指针 */
CDSL_MUTEX_UNLOCK(&d->lock);

for (size_t i = 0; i < idx; i++) {
    if (dispatch_list[i].callback) {
        dispatch_list[i].callback(key, content_snapshot, out_len, vbuf,
                                   dispatch_list[i].user_data);
    }
}
free(content_snapshot);
```

要点：
- 快照在**锁内**复制（此时 `target->content` 有效），锁外只传快照；
- OOM 处理与现有 `publish` 的 OOM 返回路径一致（解锁 + 返回 -1）；
- 三个驱动语义一致，消除"修了两个留一个"的同型缺口；
- 不改变 watch 回调的"无锁派发"契约（保持 12888de 修复的死锁消除成果）。

### 回归测试

`tests/test_distributed.c` 新增 `test_watch_content_uaf`：

- 建 in-memory / etcd-memory 源，watch key；
- 8 线程 × 200 次并发 `publish` 同一 key（每次 content 为 ~1KB 字符串，内容带序号）；
- watch 回调内**逐字节校验**收到的 content 长度与内容（`strlen` + 首尾标记），用原子计数统计合法回调次数；
- 修复前在 ASan/UBSan CI（ci.yml sanitizer job）下稳定报 heap-use-after-free；修复后全绿；
- 测试在普通 Debug 构建下也跑（校验逻辑本身可捕获大部分竞争路径）。

## 3. 修复 ②：server stop 的 drain/close 时序

### 缺陷分析

`cdsl_server_stop`（server.c:948-981）当前顺序：

1. `server_set_running(false)`
2. join listener
3. close `server_fd`
4. broadcast queue 条件变量
5. join 所有 worker
6. drain queue 中残留 socket 并 `CDSL_CLOSE_SOCKET`

竞争窗口：步骤 5 join worker 时，某 worker 正阻塞在 `CDSL_COND_WAIT(&queue_not_empty)`（server.c:779），持有 `queue_lock` 等待；步骤 4 的 broadcast 唤醒它，它 dequeue 出一个 fd 并开始 `server_handle_client`。同时步骤 6 的 drain 循环（步骤 5 之后）会 close **queue 中剩余** fd——被 worker dequeue 走的那个不在 queue 里，安全；但 worker 处理中的 fd 与 drain close 的 fd 是**不同集合**，真正的问题在于：worker 退出判定 `!server_is_running && queue_count == 0`（server.c:782）——若 stop 时 queue 非空，worker 会先处理完全部队列再退出，**但 stop 在 join worker 之前已 close 了 `server_fd`**，listener 侧无影响；worker 侧处理 fd 正常。所以残余风险集中在两点：

- **a)** drain（步骤 6）close 的 fd 可能正被**尚未 join 的 worker** 持有：步骤 5 join 保证了"所有 worker 已退出"，步骤 6 才 drain，顺序本身安全；
- **b)** 真正的缺陷是：worker 在 `CDSL_COND_WAIT` 中被 broadcast 唤醒后，`server_is_running` 已 false 且 `queue_count > 0`，worker 继续取 fd 处理——这些 fd 是 stop 前合法 enqueue 的连接，处理完才退出，**行为正确但 stop 时延不可控**；更隐蔽的是 `server_handle_client` 内部 `while (server_is_running(srv))`（server.c:714）循环，running=false 时第一次 recv 前就 break，**对已 enqueue 的连接直接不处理就 close**（server.c:777 `if (client_fd != CDSL_INVALID_SOCKET) server_handle_client(...)` → 内部循环立即退出 → close fd），客户端请求被静默丢弃而非完成。

### 改法（最小，~15 行）

引入 `srv->draining` 语义：**stop 时先让 worker 排空队列再退出，而不是提前 close 未处理连接**：

1. `cdsl_server_handle_request` 保持无状态，不动；
2. `server_worker_routine` 退出条件改为：`!running && queue_count == 0` 才 break（现状已如此，保留）；关键是让 worker 在处理 `client_fd` 时**不受 running 标志影响**——把 `server_handle_client` 中 `while (server_is_running(srv))` 改为 `while (1)`（socket 自身 EOF/错误即 break），保证已接受连接一定处理到完整请求或连接关闭；
3. stop 顺序微调：broadcast 与 join 顺序不变，但把 drain（步骤 6）移到 join worker **之前**不可行（worker 还要取 fd），维持现顺序，仅修 `server_handle_client` 的 running 判定；
4. 给 `cdsl_server_stop` 增加防御：join worker 后 drain 循环不变，确保 close 的所有 fd 均不再被任何线程使用。

净效果：stop 语义从"立即丢弃未处理连接"变为"优雅排空后关闭"，`test_server_concurrency` 现有断言（48/48 全成功）保持成立。

### 回归测试

`test_server.c` 新增 `test_server_stop_drain`：

- 启动 server（`max_concurrency = 2`）；
- 单线程连 4 个 socket 但只发前 2 个的完整请求（另 2 个保持半开）；
- 调用 `cdsl_server_stop` 计时：排空后返回；
- 断言：stop 返回后前 2 个连接收到 200 响应（或完整关闭语义），无 crash；
- 再 start 同实例验证可重启（如不支持则 free 后重建）。

## 4. 修复 ③：JIT AArch64 literal pool 偏移布局感知（jit.c:352-384）

### 缺陷分析

现状（jit.c:362-384）：

```c
instrs[n++] = 0x580000a1; /* ldr x1, #20 */      /* imm9=5 -> pc+20 */
instrs[n++] = 0x580000d0; /* ldr x16, #24 */      /* imm9=6 -> pc+24 */
...
memcpy(code, instrs, n * 4);                       /* pool at 4*n */
```

`ldr xN, [pc, #imm9*4]` 的 PC 指向**该 ldr 指令自身**，偏移相对它是字对齐的：`#20` 假设 ldr x1 位于偏移 0 的 pool 前 5 条指令处（5×4=20 字节）。prologue 现为 `stp/mov/mov/mov/ldr/ldr/blr` 共 7 条，ldr x1 是第 5 条（index 4）：pool 在 4×7=28 字节处，该 ldr 的 PC 在 4×4=16 字节处，16+20=36 ≠ 28 —— **当前代码的 `#20` 本身就是错的**（或 prologue 条数与设计文档假设不一致），AArch64 上 JIT 路径会加载到错误地址。这是布局变化未同步立即数的典型实例。

### 改法

偏移由布局**计算**而非写死：

```c
/* prologue 指令数（不含 pool） */
size_t n_pro = n;  /* 此时 n 已含 7 条 prologue，pool 位于 4*n_pro 处 */
/* LDR (literal): 0x58000000 | (imm9 << 10) | Rt */
/* imm9 = (pool_addr - ldr_pc) / 4，其中 ldr_pc = 4 * index_of_ldr */
```

具体：为两条 `ldr` 指令先占位（imm9=0），记录其 index（`idx_bc`, `idx_helper`），pool 地址 = `4 * n_pro`，每条 ldr 的 imm9 = `(4*n_pro - 4*idx) / 4` = `n_pro - idx`（恒 ≤ 8，满足 imm9 ≤ 4095 约束），回填立即数字段：

```c
instrs[idx_bc]     = 0x58000000u | ((n_pro - idx_bc)     << 10) | 0x01u; /* x1 */
instrs[idx_helper] = 0x58000000u | ((n_pro - idx_helper) << 10) | 0x10u; /* x16 */
```

prologue 增删指令时 `n_pro`/`idx` 自动变化，偏移恒正确。x86-64 路径（movabs imm64 内联）不受影响。

### 回归测试

`tests/test_jit.c` 新增（仅 `CDSL_JIT_ARCH_AARCH64` 下编译）：

- 构造含 3 条比较 + action 的 rule，编译后 `cdsl_jit_compile`，native entry 执行，结果与 threaded 引擎逐字段比对；
- 额外静态检查：dump `instrs` 数组，断言两条 ldr 的 imm9 字段等于按布局算出的值（防再次手填）。

x86 构建下该测试自动跳过（`#if defined(__aarch64__)` 守卫），CI 的 AArch64 runner（若有）覆盖；本地 x86 依赖文档化说明。

## 5. 修复 ④：test 目标 NDEBUG 空转根治（CMakeLists.txt）

### 现状核实

- 全部 CI 与本地 build 目录均 `CMAKE_BUILD_TYPE=Debug`（ci.yml 各 job 显式 `=Debug`，本地 build/build-dbg 亦 Debug），当前 `assert` 均有效；
- `tests/test_server.c` 顶部已有 `#ifdef NDEBUG / #undef NDEBUG`（test_server.c:1-3），**该写法能救 `#include <assert.h>` 之后的代码**，但只在源文件自身触发；
- 风险面：① 若用户/CI 以 Release 配置（`-DNDEBUG` 由编译器默认注入）构建 test，源文件 `#undef` 可自保，但依赖**每个测试源文件都写**该保护，属约定式防御，易漏；② `CMakeLists.txt:217-237` 的 test 目标无任何显式定义干预。

### 改法（双保险）

**事实澄清**：`NDEBUG` 的判定是**预处理层**的——编译器 `-DNDEBUG` 等价于在任何 `#include` 之前 `#define NDEBUG`；而测试源文件第 1-3 行的 `#ifdef NDEBUG / #undef NDEBUG / #endif` 位于所有 `#include` 之前，能撤销该定义，因此**仅靠这一条源文件保护本身已可根治**。CMake 侧再叠加一层显式撤销，使防御不依赖"每个源文件都记得写"的约定：

在 CMakeLists.txt 的 test 目标循环（`foreach(test_src ...)`，CMakeLists.txt:217）内，`target_compile_definitions(${test_name} PRIVATE CDSL_TESTING)` 之后追加：

```cmake
    # 测试目标恒以断言启用编译：撤销 build type 注入的 -DNDEBUG（若存在）
    if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
        target_compile_options(${test_name} PRIVATE -UNDEBUG)
    elseif(MSVC)
        target_compile_options(${test_name} PRIVATE /UNDEBUG)
    endif()
```

`-UNDEBUG`/`/UNDEBUG` 在命令行层面撤销该宏，与源文件 `#undef` 互为双保险：源文件漏写时 CMake 层兜底，CMake 层遗漏时源文件兜底。

### 回归测试

无新增测试；验证方式 = 本地以 `cmake -DCMAKE_BUILD_TYPE=Release` 单独 configure 并构建 `test_server`，`strings` 确认二进制含 `__assert_fail`（GCC；MSVC 下检查 `_CrtIsValidHeapPointer`/assert 调用桩），作为根除验证；随后 `ctest` 全绿。

## 6. 验收标准（DoD）

1. `ctest`（Debug + ASan + TSan 三个 CI 配置）45/45 全绿，新增 `test_watch_content_uaf`、`test_server_stop_drain`、AArch64 JIT pool 检查并入套件；
2. `grep -n "target->content\|->content;" src/manager/etcd.c src/manager/consul.c src/manager/config_source.c` 确认锁外派发处均为快照指针；
3. `strings <release-build>/test_server | grep __assert_fail`（GCC）非空；
4. CHANGELOG.md 新增 `## 2.0.3 (2026-11-09)` 条目，四个 fix 各自一行；
5. 文档同步：`docs/architecture.md` 的 watch 语义小节补一句"content 以快照传递"；`jit.c` 文件头注释补 AArch64 布局感知说明。

## 7. 明确不做（YAGNI）

- 不实现 etcd/consul 网络客户端；
- 不实现 JIT 逐指令机器码发射（v2.1.0 spec）；
- 不给 `cdsl_server_t` 引入 in-flight refcount（stop drain 的最小修复足够）；
- 不动 `test_distributed.c` 现有重入测试。
