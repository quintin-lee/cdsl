# Design Spec: C-DSL 完整包五项修复（arena 所有权 / HTTP CL 防御 / 64 位 LONG / 测试构建 / RCU 契约）

Date: 2026-11-09
Status: Proposed
Owner: Quintin
Scope: `src/ast/parse.c`, `include/cdsl/util/json.h`, `src/util/json.c`, `src/vm/context.c`, `src/server/server.c`, `src/ai/bridge.c`, `include/cdsl/manager.h`, `include/cdsl/ast.h`, `python/cdsl/__init__.py`, `tests/*`（**不改** `src/vm/{bytecode.c,threaded.c,jit.c}`，value_ops.c 已含完整 LONG 分支）

## 1. 背景与目标

对 C-DSL Rule Engine 做一次**完整包**修复，覆盖代码库全貌评审后核实保留的五项缺陷/债务（原 10 项经逐条源码核实，5 项判定为误判或已解决而剔除）：

| # | 缺陷 | 严重度 | 位置 |
|---|------|--------|------|
| 1 | `cdsl_parse_string` 的 `errors` 路径保留 partial rule 却不释放局部 arena（确定性泄漏），且细粒度 `cdsl_free_expr` 在 thread-local arena 被重置后走逐节点 `free` 去 free arena 内存 = UB 契约陷阱 | 高 | parse.c:85-108 |
| 2 | `http_get_content_length` 无上限校验，伪造超大 Content-Length 导致 reader 读不满/挂起/内存耗尽；10MB 上限硬编码散落 | 中 | server.c:59-79, 717-754 |
| 4 | `cdsl_json_value_t.number` 仅 `double`，64 位整数 >2^53 丢精度，`CDSL_TYPE_LONG` 永远不会被产生 | 中 | json.h, json.c:374, context.c:1020+1044, server.c:364-372, bridge.c:1137 |
| 3 | manager RCU 的 acquire/release 线程安全契约未文档化（代码本身健全） | 低（文档） | manager.c / include/cdsl/manager.h |

**目标**：① partial rule 的所有权契约显式化并消除 UB；② HTTP 请求体入口拒绝超大 Content-Length（413）；③ 数值层彻底支持 64 位 LONG 全流程（JSON 解析 → 值映射 → value_ops 求值）；④ RCU 配对契约文档化。**非目标**：不实现 etcd/consul 真客户端，不引入 ctest，不做 JIT 逐指令机器码发射，不改变现有 VM 语义。

### 已剔除项（不修）

以下 6 项经源码核实为误判、已解决或现状已满足，本 spec **不修**：

| 项 | 核实结论 |
|----|----------|
| JIT W^X | `src/vm/jit.c` 已做完整 `mmap RW → 写入 → mprotect RX → clear_cache`，无 W^X 缺陷 |
| canary dry-run | `src/manager/canary.c` 的 `cdsl_canary_verify_dsl` 已检查 `rpt->status == ERROR` 后再部署 |
| obs 环形缓冲 | `src/obs/` 仅 `metrics.c` + `trace.c`，无环形缓冲实现 |
| umbrella 头 | `include/cdsl/cdsl.h` 已聚合 28 个头 |
| instructions.inc 双分发 | CMake 双 `-I` 注入为既定设计，非缺陷 |
| CMake 测试构建 CONFIGURE_DEPENDS | `CMakeLists.txt:216` 现状已是 `file(GLOB TEST_SOURCES CONFIGURE_DEPENDS ...)`，**修复⑤ 为 no-op**，现状已满足 |

## 2. 修复 ①：parse.c 部分成功路径的 arena 所有权

### 缺陷分析

`parse.c` 的 `errors && rule != NULL` 分支（L85-108）保留 partial `rule` 但**不释放**局部 `arena`。同时 `rule->arena` 在 `ast.c:562/587` 已被指向局部 `arena`。该缺陷分两层：

- **(a) 确定性泄漏**：`errors && rule` 路径下，L88 只对 `!rule` 释放 arena，末尾 `if (!rule && arena)` 兜底在 `rule != NULL` 时不触发 → arena 随 partial rule 返回后无人释放（caller 若不调 `cdsl_free_rule` 则泄漏；当前全库无 caller 在 errors 路径对 partial rule 调细粒度 free，`manager.c:178` 走 `cdsl_free_rule`，故**泄漏在当前代码内已被 cdsl_free_rule 消化，真正的缺陷是契约未文档化**）。
- **(b) 潜在 UB 契约陷阱**：caller 若调细粒度 `cdsl_free_expr(rule->when_expr)` → 此刻 `current_ast_arena` 在 L111 已置 `NULL` → 走"逐节点 `free()`"分支去 free **arena 内存** = **UB / 崩溃**（当前无 caller 触发，但 API 陷阱 + 文档缺失）。

缺陷定性：**高 / 泄漏 + API 契约陷阱**（非现存崩溃）。

### 改法

在 `errors && rule` 分支中，把局部 `arena` **挂到 `rule->arena`**（若 parser 尚未设置），并显式转移所有权、置 `arena = NULL` 防止下方 double-free。保留现有代码结构小改（不整块重写）：

```c
/* parse.c L85-108 局部小改 */
if (errors && rule && !rule->arena) {
    rule->arena = arena;   /* 挂局部 arena 到 partial rule，转移所有权 */
    arena = NULL;         /* 防止下方 if (!rule && arena) 与 cdsl_free_rule 双 free */
}
```

配套所有权契约写入 `ast.h` 的 `cdsl_parse_string` 文档（L682-695）：

> When a partial rule is returned alongside errors, the caller takes ownership and MUST free it with `cdsl_free_rule()`; fine-grained `cdsl_free_expr()` must not be called on it, because the per-node arena is no longer reachable via the thread-local stack. The arena is attached to `rule->arena` on the errors path so that `cdsl_free_rule` reclaims it safely.

### 回归测试

`tests/test_arena_ownership.c`（新）：构造会 partial-parse 的 DSL（缺表达式的规则），在 `errors` 路径下取到 partial rule，ASan 下验证：

- (a) `cdsl_free_rule(rule)` 干净退出（无 leak / heap-use-after-free）；
- (b) partial rule 为 **NULL**（完全解析失败）的 errors 路径 arena 不泄漏；
- (c) 旧 UB 路径（`cdsl_free_expr` 拆 arena 节点）**不再被任何 caller 触发**——grep 全库确认无残余调用点，文档约束到位。

## 3. 修复 ②：server.c Content-Length 上限与 10MB 扩容常量

### 缺陷分析

- `http_get_content_length`（L59-79）返回 `long`，解析 CL 头**无上限校验**；若客户端伪造 `Content-Length: 999999999`，`server_handle_client` 读循环（L714）会把 `content_len` 设为伪造大数 → `len >= h_len + content_len` 永远不成立 → 循环只能靠 `cap >= 10MB` 阈值 break（L716）或 `recv` 失败退出，逻辑上读不满预期 body 即 break，但 10MB 阈值在伪造大 CL 时**过早 break**（cap 扩到 10MB 即停，此时 body 远未读满）→ 后续 `cdsl_server_handle_request` 拿到残缺 body。
- 10MB 上限**硬编码**在 `L716`（`if (cap >= (size_t)10 * 1024 * 1024)`），散落魔法数字。
- 400/404 响应均为**内联字符串拼接**（L264/L694）+ `Connection: close`，无 `send_http_status` 辅助函数。

### 改法

1. 把 10MB 提成 `server.c` 顶部宏：

   ```c
   #define CDSL_SERVER_MAX_BODY_BYTES  (10u * 1024u * 1024u)
   ```

   `L716` 的硬编码 `10 * 1024 * 1024` 替换为 `CDSL_SERVER_MAX_BODY_BYTES`。

2. 在 `server_handle_client` 的读循环内（L739 `content_len = http_get_content_length(...)` 之后），CL 解析完成时立即校验上限，**选 413 拒绝**（非 clamp）：

   ```c
   if (header_end) {
       content_len = http_get_content_length(req_buf, ...);
       if (content_len > 0 &&
           (size_t)content_len > CDSL_SERVER_MAX_BODY_BYTES) {
           const char* resp_413 =
               "HTTP/1.1 413 Payload Too Large\r\n"
               "Content-Length: 0\r\nConnection: close\r\n\r\n";
           send(client_fd, resp_413, strlen(resp_413), 0);
           CDSL_CLOSE_SOCKET(client_fd);
           return;
       }
   }
   ```

   注入点选 L739 而非 L716：CL 只在 header_end 锁定后解析一次（L735-739），此后 `content_len` 不再变化，在此处 clamp 判定最简洁且不会在扩容循环中反复触发。413 后**沿用现有 `Connection: close` + 无条件 `CDSL_CLOSE_SOCKET(client_fd)` 语义**（与 L264/L694 的 400/404 一致），无需 keep-alive。

   413 判定只 reject `> MAX`；**负数 / 0 / 缺 CL 头保持现有 `content_len <= 0` → break 短路语义不变**（不扩大改动面）。

### 回归测试

`tests/test_http_limits.c`（新）：

- 伪造 `Content-Length: 100000000`（> 10MB）→ 断言 413 + 连接 close；
- `Content-Length` == 10MB（== 上限）→ 断言 200（边界钉住，防回归把 == 误判为 >）；
- 正常 10MB-1 字节 body → 断言 200；
- `Content-Length: 0` / 负数 / 缺 CL 头 → 断言保持现有短路（`content_len <= 0` 语义不变）；
- 起真 server 进程 curl 验证 413 响应。

## 4. 修复 ④：int/long 全流程 — 扩展 `cdsl_json_value_t` 的 number union

### 缺陷分析

- `cdsl_json_value_t`（json.h:33）的 number union 只有 `double number_val`，**没有 `int64_t` 字段**；
- `json.c:374` 的 `parse_value` 数字路径用 `strtod` 解析，64 位整数 >2^53 在 `double` 下**丢精度**；
- `load_json_recursive`（context.c L1020 对象分支 + L1044 数组元素分支）用 `(double)(int)v != v` 判 int/float，**两处**都把数字映射到 `CDSL_TYPE_INT`/`CDSL_TYPE_FLOAT`，LONG 类型**永远不会被产生**；
- server.c L364-372 内联 JSON 填充同样用 `(double)(int64_t)it->value.number_val == ...` 判 int/float；
- bridge.c L1137 的 `risk_score` 用 `(int)v->value.number_val`。

### 改法（最小但彻底）

**事实澄清**：`src/vm/value_ops.c` **已含完整 LONG 分支**（`ck_add_i64`/`ck_sub_i64`/`ck_mul_i64`、L284-288 的 LONG 精确算术、L94 的 LONG 日期 delta），threaded.c 是**指令分发器**、jit.c 是**跳板**（均不含逐指令数值代码生成），**三套 VM 求值层零改动**。真实改动面 = JSON 解析层 + 值映射层 + 字符串化/外部读取点：

1. **`json.h` — 扩展 union**（L33 的 `number_val` 字段）：

   ```c
   union {
       double    f64;   /* 浮点（原 number_val 改名） */
       int64_t   i64;   /* 64 位整数 */
       /* string / object / array 指针字段保持原样 */
   } value;
   ```

   原 `number_val` 改名 `f64`，新增 `i64`。全库 `value->number_val` 访问点（grep 已核实共 12 处）→ `value->value.f64` 或 `value->value.i64`（编译强制走一遍）。

2. **`json.c` — `parse_value` 数字路径**（L374 附近，strtoq/strtod 双路）：

   - 先尝试 `strtoq` 解析为 `int64_t`（覆盖纯整数）；成功则填 `value.i64` + 判 `type = CDSL_JSON_INT`；
   - 失败（含小数/科学计数/溢出）则 `strtod` 解析为 `double`（覆盖 `f64`）；
   - **int64 溢出协议（定案：降级 FLOAT）**：若 `strtoq` 报 `ERANGE`（如 `9223372036854775808` 即 INT64_MAX+1），**不报错**，落 `strtod` 路径填 `f64`、`type = CDSL_JSON_FLOAT`——与现有"数字恒可解析"契约一致，精度丢失仅发生在 >INT64_MAX 的极罕见边界，对 LLM `risk_score`（0-100）等字段无影响。明文规定：`type` 由 `i64`/`f64` 填充成功与否决定，INT 与 FLOAT 二选一，无第三态。

3. **`context.c` — `load_json_recursive` 两处映射**：

   - **对象分支（L1020-1024）**：`child->type == CDSL_JSON_NUMBER` 时，若 JSON 标记为 INT（`strtoq` 成功）且 `i64` 超出 32 位 `INT` 范围（`> INT_MAX || < INT_MIN`）→ 调 `cdsl_context_set_long(ctx, key, i64)` 映射 `CDSL_TYPE_LONG`；32 位可容纳 → `cdsl_context_set_int`（不变）；FLOAT → `cdsl_context_set_float`（不变）。
   - **数组元素分支（L1044-1050）**：同型改法，`elem->type == CDSL_JSON_NUMBER` 时按 INT/FLOAT 标记 + 32 位可容纳性写 `arr->items[idx].type`（INT → `CDSL_TYPE_INT`，超 32 位 INT → `CDSL_TYPE_LONG`，FLOAT → `CDSL_TYPE_FLOAT`）。

   需确认 `cdsl_context_set_long` 是否存在；若仅有 `set_int`/`set_float`，补一个 `cdsl_context_set_long`（写 `CDSL_TYPE_LONG` + `long_val`）。

4. **`server.c` L364-372 — 内联 JSON 填充**：同步改法，`INT` 值经 `i64` 读入后判 32 位可容纳性 → `CDSL_TYPE_INT` 或 `CDSL_TYPE_LONG`；`FLOAT` 不变。

5. **`bridge.c` L1137**：`(int)v->value.number_val` → 按 INT/FLOAT 标记改 `(int)v->value.f64`（`risk_score` 是 0-100 小整数，INT 标记，语义不变）。

6. **`ast.h` 的 `cdsl_type_t`** 已含 `CDSL_TYPE_LONG`（value_ops.c L71/94 已用），无需改。

7. **test_json.c 适配**：L119/139/144/181/489 共 5 处 `->value.number_val` 断言 → 按 INT/FLOAT 标记改 `->value.f64` 或 `->value.i64`。

8. **python 绑定**：`python/cdsl/__init__.py` 的 ctypes 结构布局需同步 `cdsl_json_value_t` 的 union 扩展（新增 `i64` 字段后 struct 大小变化）。

### 回归测试

- `tests/test_json_int64.c`（新）：
  - `"9223372036854775807"`（LLONG_MAX）→ 断言 `v.i64 == INT64_MAX`、`type == CDSL_JSON_INT`；
  - `"9223372036854775808"`（INT64_MAX+1，溢出协议）→ 断言 `type == CDSL_JSON_FLOAT`、`v.f64` 降级；
  - `"-9223372036854775808"`（LLONG_MIN）→ 断言 `v.i64 == INT64_MIN`；
  - `"9.2"` → 断言 `type == CDSL_JSON_FLOAT`、`v.f64`；
  - `"42"` → 断言 `v.i64 == 42`、`type == CDSL_JSON_INT`；
  - `"0"` → 断言 `v.i64 == 0`、`type == CDSL_JSON_INT`。
- `tests/test_json.c`（扩）：`load_json_recursive` 输入 `{"x": 2147483648}`（> INT_MAX）→ 断言 `CDSL_TYPE_LONG`；`{"x": 42}` → `CDSL_TYPE_INT`；`{"x": 9.2}` → `CDSL_TYPE_FLOAT`；数组元素 `[[10000000000, 42]]` → 断言首元素 LONG、次元素 INT。
- value_ops.c 的 LONG 算术已有覆盖（ck_add_i64 等），无需新测试；若 UBSan 下补 INT_MAX/INT_MIN/LLONG_MAX/LLONG_MIN 边界运算用例更佳。

## 5. 修复 ⑤：CMakeLists.txt 测试构建（现状核实为 no-op，仅验证）

### 现状核实

`cdsl_objects` OBJECT 库被 static + shared 复用（L100/139/160），`file(GLOB TEST_SOURCES CONFIGURE_DEPENDS "tests/test_*.c")`（L216）**现状已含 `CONFIGURE_DEPENDS`**——新增 `test_*.c` 时 CMake 已能自动重配置，无需手动 `cmake -S . -B build`。测试 target 亦已统一 `target_link_libraries(... PRIVATE cdsl_static)`（L239）。

**本项为 no-op**，现状已满足 DoD 第 6 条。唯一保留动作：把"验证 `test_dummy.c` 能被 `make` 自动拉取"并入 §7 的集成层回归，**不单独新增 test_dummy 文件**（避免为验证项引入无意义测试）。

**不做**：不引入 ctest（38 个 `main`+注册的微框架已工作，迁移成本 > 收益）。

### 回归测试

新增任意 `tests/test_*.c` 后只跑 `make -C build`（无 cmake re-run）验证 CONFIGURE_DEPENDS 生效（作为 ①②④ 新增测试文件的顺带验证，不单独建 test_dummy）。

## 6. 修复 ③：manager.c RCU 契约文档化

### 现状核实

`acquire`/`release` 用 `CDSL_ATOMIC_FETCH_ADD/SUB` + `ref_count==1` 守门，RCU 配对本身健全，**无需代码修复**。缺的是**契约文档**——caller 在哪个线程持锁、`deploy_canary` 路径下 RCU 读侧与写侧的同步点、是否允许跨线程 free。

### 改法（纯文档）

在 `include/cdsl/manager.h` 的 `cdsl_manager_acquire` / `cdsl_manager_release` 函数注释处补线程安全契约（**release 可跨线程，非 SAME-thread**，与实际 worker 线程用法一致）：

```c
/**
 * cdsl_manager_acquire / cdsl_manager_release — RCU contract
 *
 *  - acquire is an atomic refcount bump (fetch_add). It MAY be called
 *    from any reader thread.
 *  - release is an atomic refcount drop (fetch_sub) and is thread-safe:
 *    the releasing thread MUST be the one that owns the last use of the
 *    acquired snapshot (reader releases its own ref after it is done
 *    dereferencing snap; cross-thread release is permitted because the
 *    operation is atomic, but the reader MUST NOT dereference the
 *    snapshot after calling release).
 *  - The ruleset is recycled only in the deploy_canary / destroy path,
 *    guarded by manager->deploy_mutex, and ONLY after ref_count has
 *    reached 0 (no live reader). No thread may free the old ruleset's
 *    arena while any reader still holds a ref. (deploy_mutex 在实现中为
 *    manager.c 的 write_lock 互斥，回收路径在该锁内执行。)
 *  - Violation (freeing a ruleset while ref_count > 0) = use-after-free
 *    on the ruleset's arena.
 */
```

关键措辞修正：原草案"SAME thread release"与 manager 实际用法（worker 线程各自 acquire/release，release 是原子操作可跨线程）矛盾；改法明确 release 线程安全、约束点在"reader release 后不得再解引用 snap"，而非"必须同线程"。

### 回归测试

无新增测试（文档不改变行为）。可选 `tests/test_rcu_contract.c`——ASan 下模拟跨线程 free 场景，断言契约被违反时崩溃（regression 守卫，**非必选**）。

## 7. 测试策略

**分层验证矩阵**（4 项有效修复 × 4 层）：

| 修复项 | 单元测试 | ASan 层 | 集成层 | 模糊层 |
|---|---|---|---|---|
| ① arena 所有权 | `test_arena_ownership.c`（新） | ASan 确认无 leak/UAF | `test_ast.c` 回归 | 扩 `fuzz_parse.c` 语料 |
| ② HTTP CL 防御 | `test_http_limits.c`（新） | 同左 | 起真 server curl 验证 | 不适用 |
| ④ 64 位 LONG | `test_json_int64.c`（新）+ `test_json.c`（扩） | ASan + UBSan 跑数值边界 | value_ops 已有 LONG 回归 | 扩 `fuzz_parse.c` 64 位边界 |
| ③ RCU 契约 | 无（纯文档） | 可选 `test_rcu_contract.c` | `test_manager.c` 回归 | 不适用 |

注：`test_value_types.c` 不存在（已剔除），数值映射测试并入 `test_json.c`（已存在）；`fuzz_parse.c` 不被 `file(GLOB tests/test_*.c)` 收录（fuzz 目标独立编译），其语料扩展在 `CDSL_BUILD_FUZZ=ON` 构建下验证，不计入常规 ctest 矩阵。

**执行纪律**：

- 每次提交前：`ccmake build && ctest --output-on-failure -R <相关 pattern>`（默认全绿门槛）；
- 涉及内存所有权的改动（①）：ASan 必跑（`CMAKE_BUILD_TYPE=Debug` + `-fsanitize=address -fno-omit-frame-pointer`）；
- 涉及数值的改动（④）：UBSan 必跑（`-fsanitize=undefined` 加到 Debug 构建）。

## 8. 验收标准（DoD）

1. `ccmake build && ctest` 全绿，且新增测试全部通过（① `test_arena_ownership`、② `test_http_limits`、④ `test_json_int64` 与扩展的 `test_json.c`）；
2. ASan 跑 ① 用例：`cdsl_free_rule` 路径零报错、零 leak；旧 UB 路径（细粒度 free arena 节点）不再被任何 caller 触发（grep 全库确认）；
3. `test_json_int64` 断言 LLONG_MAX 精确到 int64：`"9223372036854775807"` 解析后 `v.i64 == INT64_MAX`、`type == CDSL_JSON_INT`；`"9223372036854775808"` 溢出协议降级 FLOAT；value_ops 的 LONG 加减乘模结果一致；
4. 边界映射：`load_json_recursive` 输入 `2147483648`（> INT_MAX）→ `CDSL_TYPE_LONG`；`42` → `CDSL_TYPE_INT`；`9.2` → `CDSL_TYPE_FLOAT`；数组元素 `[[10000000000, 42]]` 首元素 LONG、次元素 INT；
5. HTTP 413：伪造 `Content-Length: 100000000` 的 curl 请求返回 413 + 连接 close；`CDSL_SERVER_MAX_BODY_BYTES` 宏在 `server.c` 中仅用于 L716 扩容判断与 413 判定两处（grep 确认原硬编码 `10 * 1024 * 1024` 已清除、无第三处散落）；
6. CMake CONFIGURE_DEPENDS：新增任意 `tests/test_*.c` 后只跑 `make` 自动拉取，无 cmake re-run（现状 L216 已满足，验证性）；
7. RCU 契约：`include/cdsl/manager.h` 的 acquire/release 注释含上述线程安全契约段（read 验证，release 跨线程语义）。

## 9. 改动面总览

- **必改文件**：`src/ast/parse.c`, `include/cdsl/util/json.h`, `src/util/json.c`, `src/vm/context.c`, `src/server/server.c`, `src/ai/bridge.c`, `include/cdsl/manager.h`
- **④ 数值映射（非 VM 镜像）**：`context.c` 两处（L1020 对象 + L1044 数组元素），如需补 `cdsl_context_set_long`；`server.c` L364-372；`bridge.c` L1137；**不**改 `src/vm/{bytecode.c,threaded.c,jit.c}`（value_ops.c 已含完整 LONG 分支，threaded/jit 是分发器/跳板）
- **新增测试文件**：`tests/test_arena_ownership.c`, `tests/test_http_limits.c`, `tests/test_json_int64.c`
- **扩展测试文件**：`tests/test_json.c`（数值映射 + 5 处 `number_val` 断言适配）, `tests/test_ast.c`（arena 回归）, `tests/fuzz_parse.c`（语料，fuzz 构建下验证）
- **绑定适配**：`python/cdsl/__init__.py`（ctypes 结构布局同步 union 扩展）
- **纯文档**：`include/cdsl/manager.h`（RCU 契约注释）, `include/cdsl/ast.h`（parse_string 所有权契约注释）
- **no-op**：`CMakeLists.txt`（⑤ 现状已满足，不改）

**预估改动量**：非测试 ~300 行（union 改名 ~12 处机械 + json.c 双路解析 ~40 + context.c 两处映射 + server.c 映射 + bridge.c + 413 注入 + parse.c 所有权 + manager.h/ast.h 文档）+ 3 个新测试文件 ~250 行 + test_json.c 适配。

## 10. 明确不做（YAGNI）

- 不实现 etcd/consul 网络客户端；
- 不引入 ctest；
- 不做 JIT 逐指令机器码发射；
- 不改 `src/vm/{bytecode.c,threaded.c,jit.c}`（value_ops.c 已含完整 LONG 分支）；
- 不修 6 项已核实为误判/已解决/现状已满足的项（JIT W^X、canary dry-run、obs 环形缓冲、umbrella 头、instructions.inc 双分发、CMake CONFIGURE_DEPENDS）；
- 不给 `cdsl_server_t` 引入 in-flight refcount（② 的最小修复足够）；
- 不单独建 `test_dummy.c`（⑤ 为 no-op，验证并入 ①②④ 新增文件的顺带回归）。
