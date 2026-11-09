# C-DSL 完整包五项修复实施计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 实施 `docs/superpowers/specs/2026-11-09-cdsl-fixes-design.md` 的完整包五项修复：① parse.c arena 所有权、② server.c HTTP 413 防御、④ 64 位 LONG 全流程（JSON 解析层 + 值映射层，**不改** 三套 VM 求值层）、③ manager RCU 契约文档化。⑤ CMake CONFIGURE_DEPENDS 为 no-op（现状已满足），仅验证。

**Architecture:** 详见 spec §1-§10。本计划按 4 个独立子任务分解（每个子任务自成提交单元，可单独验证），按依赖顺序执行：JSON union 扩展是 ④ 的前置，故 ④ 内部先 json.h 再 json.c/context.c。

**Tech Stack:** C11（CMake 3.19+，`-Wall -Wextra -Wpedantic -Werror -Wshadow`），手写微测试框架（`tests/test.h`，38 个 `test_*.c`），ASan/UBSan 验证。

---

## 执行顺序与依赖图

```
Task 1 (④ JSON union + 解析层)  ─┐
                                  ├─→ Task 2 (④ 值映射层: context.c/server.c/bridge.c/test_json.c/python)
Task 1b (④ value_ops 回归确认)   ─┘
Task 3 (① parse.c arena 所有权)   ← 独立
Task 4 (② server.c 413 防御)     ← 独立
Task 5 (③ RCU 契约文档)          ← 独立
Task 6 (⑤ CMake no-op 验证 + 全量回归)
```

每完成一个 Task：`ccmake build && ctest`（或增量 `make -C build && ctest --output-on-failure -R <pattern>`）保持全绿，再 commit。

---

## Task 1: ④ 扩展 `cdsl_json_value_t` union + 双路数字解析

**Files:**
- Modify: `include/cdsl/util/json.h` (L33 的 `number_val` 字段)
- Modify: `src/util/json.c` (L374 的 `strtod` 数字路径)

- [ ] **Step 1: 扩展 json.h 的 number union**

定位 `include/cdsl/util/json.h` L33 的 `double number_val;`，把 number union 改为：

```c
union {
    double    f64;   /* 浮点（原 number_val 改名） */
    int64_t   i64;   /* 64 位整数 */
} /* number */;
```

注意：原 union 内含 `number_val` 与 `bool_val`/`string_val`/object/array 指针字段（以实际 json.h L28-45 为准），**只在 number 那一个 union 内加 `i64`**，其余字段不动。若 number 不是独立 union 而是结构体字段组，则按实际布局在对应位置加 `int64_t i64;`。

- [ ] **Step 2: json.c 双路数字解析（strtoq/strtod）**

定位 `src/util/json.c` L374 附近的 `parse_value` 数字路径（现状 `strtod`），改为：

```c
/* 数字解析：先试 strtoq（纯整数 → i64 + CDSL_JSON_INT），
 * 失败（含小数/科学计数/溢出 ERANGE）则 strtod（→ f64 + CDSL_JSON_FLOAT）。
 * 溢出协议定案：strtoq 报 ERANGE 时降级 FLOAT，不报错（与"数字恒可解析"契约一致）。 */
char* end = NULL;
errno = 0;
int64_t iv = strtoq(p->src + p->pos, &end, 10);
if (end && *end == '\0' && errno == 0 && end != (p->src + p->pos)) {
    /* 纯整数且未溢出 → INT 标记 */
    v->value.i64 = iv;
    /* type = CDSL_JSON_NUMBER，标记 is_int（见 Step 3） */
} else if (strpbrk(p->src + p->pos, ".eE") == NULL) {
    /* 整数但溢出 ERANGE → 降级 FLOAT */
    v->value.f64 = strtod(p->src + p->pos, &end);
    /* type = CDSL_JSON_NUMBER，标记 is_float */
} else {
    /* 含小数/科学计数 → FLOAT */
    v->value.f64 = strtod(p->src + p->pos, &end);
    /* type = CDSL_JSON_NUMBER，标记 is_float */
}
```

**注意**：现有代码用单一 `CDSL_JSON_NUMBER` type 区分 int/float（spec §4 改法第 2 条说"`type` 设为 `CDSL_JSON_INT` 或 `CDSL_JSON_FLOAT`"，但 json.h 枚举现状需核实——**若现状只有 `CDSL_JSON_NUMBER` 无 `CDSL_JSON_INT`/`CDSL_JSON_FLOAT` 枚举，则需**：① 在 json.h 枚举加 `CDSL_JSON_INT`/`CDSL_JSON_FLOAT` 两个值，**或** ② 用 `cdsl_json_value_t` 加一个 `int is_int;` 标志字段 + 保留 `CDSL_JSON_NUMBER` 单一 type。选 ② 更轻（不动 type 枚举与全库 `CDSL_JSON_NUMBER` 比较点，spec §9 提到 grep 已核实全库有 12 处 `number_val` 访问点 + 多处 `CDSL_JSON_NUMBER` 比较点）。**实施时先 grep `CDSL_JSON_NUMBER` 全库出现数，若 < 5 处选 ①（加枚举），否则选 ②（加 `is_int` 标志）并在此处定案。**

- [ ] **Step 3: 全库 `number_val` 访问点改名（编译强制走一遍）**

`grep -rn "number_val" --include=*.c --include=*.h .` 已核实共 12 处（test_json.c:119/139/144/181/489、bridge.c:1137、server.c:364-372、context.c:1020/1044、json.c:374、json.h:33）。按 Step 1 的 union 布局把 `->value.number_val` 改为 `->value.f64`（浮点读取）或 `->value.i64`（整数读取）。本 Task 只改 json.h/json.c；context.c/server.c/bridge.c/test_json.c 的改名留到 Task 2（避免 Task 1 后中间态编译不过——**实际上若 Task 1 改了 json.h 的字段名而 context.c 等还没改，全库编译会失败**。所以 Task 1 必须**连同** context.c/server.c/bridge.c/test_json.c 的 `number_val`→`f64/i64` 改名一起做，才能在 Task 1 结束时保持可编译）。

**Task 1 实际覆盖**：json.h + json.c + context.c(1020/1044) + server.c(364-372) + bridge.c(1137) + test_json.c(119/139/144/181/489) 的 `number_val`→`f64`/`i64` 改名 + json.c 双路解析。**这是 ④ 的"union 扩展 + 改名"原子单元**，必须在一次提交内完成，否则中间态编译不过。

- [ ] **Step 4: 验证编译 + 现有 test_json.c 全绿**

```bash
ccmake build && ctest --output-on-failure -R test_json
```

`test_json.c` 现有断言（age/score/elem 等）在改名后语义不变（都是小整数，`f64` 读 25/100/87/42 仍正确）→ 全绿。`grep "number_val" 全库` 应返回 0 命中（除 json.h 定义本身已被改）。

- [ ] **Step 5: commit**

```bash
git add -A && git commit -m "feat(json): extend cdsl_json_value_t number union with int64, dual-path parse"
```

---

## Task 1b: ④ 值映射层改法（INT 超 32 位 → LONG）

**Files:**
- Modify: `src/vm/context.c` (L1020 对象分支 + L1044 数组元素分支)
- Modify: `src/server/server.c` (L364-372 内联 JSON 填充)
- Modify: `src/ai/bridge.c` (L1137 risk_score)

- [ ] **Step 1: context.c 对象分支（L1019-1024）加 LONG 映射**

现状：
```c
} else if (child->type == CDSL_JSON_NUMBER) {
    double v = child->value.number_val;
    if (v != (double)(int)v) {
        cdsl_context_set_float(ctx, key, v);
    } else {
        cdsl_context_set_int(ctx, key, (int)v);
    }
}
```

改为（用 Task 1 的 INT/FLOAT 标记 + 32 位可容纳性判定）：
```c
} else if (child->type == CDSL_JSON_NUMBER) {
    if (is_int_marker) {           /* 纯整数路径 */
        int64_t iv = child->value.i64;
        if (iv > INT_MAX || iv < INT_MIN) {
            cdsl_context_set_long(ctx, key, iv);   /* 超 32 位 → LONG */
        } else {
            cdsl_context_set_int(ctx, key, (int)iv);
        }
    } else {                        /* 浮点路径 */
        cdsl_context_set_float(ctx, key, child->value.f64);
    }
}
```

`cdsl_context_set_long` 已存在（spec §2 review round-2 核实 context.c:834、context.h:183 已有）——直接调用，无需补。

- [ ] **Step 2: context.c 数组元素分支（L1043-1051）同型改法**

现状：
```c
if (elem->type == CDSL_JSON_NUMBER) {
    double nv = elem->value.number_val;
    if (nv != (double)(int)nv) {
        arr->items[idx].type = CDSL_TYPE_FLOAT;
        arr->items[idx].data.float_val = nv;
    } else {
        arr->items[idx].type = CDSL_TYPE_INT;
        arr->items[idx].data.int_val = (int)nv;
    }
}
```

改为：
```c
if (elem->type == CDSL_JSON_NUMBER) {
    if (is_int_marker) {
        int64_t iv = elem->value.i64;
        if (iv > INT_MAX || iv < INT_MIN) {
            arr->items[idx].type = CDSL_TYPE_LONG;
            arr->items[idx].data.long_val = iv;
        } else {
            arr->items[idx].type = CDSL_TYPE_INT;
            arr->items[idx].data.int_val = (int)iv;
        }
    } else {
        arr->items[idx].type = CDSL_TYPE_FLOAT;
        arr->items[idx].data.float_val = elem->value.f64;
    }
}
```

`CDSL_TYPE_LONG` 的 `data.long_val` 字段已存在（value_ops.c L94 已用 `delta->data.long_val`）——直接用。

- [ ] **Step 3: server.c L364-372 内联 JSON 填充同型改法**

现状（spec 核实）：
```c
if ((double)(int64_t)it->value.number_val == it->value.number_val) {
    cdsl_context_set_int(ctx, it->key, (int)(int64_t)it->value.number_val);
} else {
    cdsl_context_set_float(ctx, it->key, it->value.number_val);
}
```

改为（INT 标记 + 32 位可容纳性）：
```c
if (is_int_marker) {
    int64_t iv = it->value.i64;
    if (iv > INT_MAX || iv < INT_MIN) cdsl_context_set_long(ctx, it->key, iv);
    else                               cdsl_context_set_int(ctx, it->key, (int)iv);
} else {
    cdsl_context_set_float(ctx, it->key, it->value.f64);
}
```

- [ ] **Step 4: bridge.c L1137 risk_score 适配**

现状：`rev->risk_score = (v->type == CDSL_JSON_NUMBER) ? (int)v->value.number_val : 0;`

改为：`rev->risk_score = (v->type == CDSL_JSON_NUMBER && is_int_marker) ? (int)v->value.i64 : 0;`

（risk_score 是 LLM 输出的 0-100 小整数，INT 标记，语义不变；若 LLM 返回浮点 risk_score，原来 `(int)v->value.number_val` 是截断，现在 `is_int_marker` 为 false 则走 else 分支取 0——**需保留原截断语义**：改为 `(int)v->value.f64` 而非 0，即 `rev->risk_score = (v->type == CDSL_JSON_NUMBER) ? (int)(is_int_marker ? v->value.i64 : (int64_t)v->value.f64) : 0;`。）

- [ ] **Step 5: 验证 test_json.c + 新增 test_json_int64.c**

写 `tests/test_json_int64.c`（spec §4 回归测试）：
- `"9223372036854775807"`（LLONG_MAX）→ 断言 `v.i64 == INT64_MAX`、INT 标记；
- `"9223372036854775808"`（INT64_MAX+1，溢出协议）→ 断言 FLOAT 标记、`v.f64` 降级；
- `"-9223372036854775808"`（LLONG_MIN）→ 断言 `v.i64 == INT64_MIN`、INT 标记；
- `"9.2"` → 断言 FLOAT 标记、`v.f64`；
- `"42"` → 断言 `v.i64 == 42`、INT 标记；
- `"0"` → 断言 `v.i64 == 0`、INT 标记。

写 `tests/test_json.c` 扩展用例：
- `{"x": 2147483648}`（> INT_MAX）→ 断言 `CDSL_TYPE_LONG`；
- `{"x": 42}` → 断言 `CDSL_TYPE_INT`；
- `{"x": 9.2}` → 断言 `CDSL_TYPE_FLOAT`；
- 数组 `[[10000000000, 42]]` → 断言首元素 `CDSL_TYPE_LONG`、次元素 `CDSL_TYPE_INT`。

```bash
ccmake build && ctest --output-on-failure -R "test_json"
```

全绿（CONFIGURE_DEPENDS 现状 L216 已自动拉新文件，无需手动 cmake re-run——**验证 ⑤ no-op**）。

- [ ] **Step 6: UBSan 跑数值边界**

```bash
CMAKE_BUILD_TYPE=Debug CFLAGS="-fsanitize=address,undefined" ccmake build && ctest --output-on-failure -R "test_json"
```

INT_MAX/INT_MIN/LLONG_MAX/LLONG_MIN 边界无 UBSan 报错。

- [ ] **Step 7: commit**

```bash
git add -A && git commit -m "feat(vm/context,server,ai): map int64 JSON numbers to CDSL_TYPE_LONG when >32-bit"
```

---

## Task 2: ① parse.c arena 所有权修复

**Files:**
- Modify: `src/ast/parse.c` (L85-108 的 `errors && rule` 分支)
- Modify: `include/cdsl/ast.h` (`cdsl_parse_string` 文档 L682-695)
- Create: `tests/test_arena_ownership.c`

- [ ] **Step 1: parse.c 局部小改（不整块重写）**

定位 parse.c L85-108 的 `errors && rule` 分支（现状 L88 只对 `!rule` 释放 arena），在保留现有结构前提下方追加所有权转移：

```c
/* errors && rule 路径：把局部 arena 挂到 partial rule，转移所有权 */
if (errors && rule && !rule->arena) {
    rule->arena = arena;   /* 挂局部 arena 到 partial rule */
    arena = NULL;         /* 防止下方 if (!rule && arena) 与 cdsl_free_rule 双 free */
}
```

末尾 `if (!rule && arena) { cdsl_arena_free(arena); arena = NULL; }` 保持不变（errors+rule 时 `arena` 已 NULL，不触发；fail-fast 时 rule/arena 均清，不触发）。

- [ ] **Step 2: ast.h 所有权契约注释**

定位 `include/cdsl/ast.h` `cdsl_parse_string` 的文档（L682-695），追加：

```
 * @note When a partial rule is returned alongside errors, the caller takes
 *       ownership and MUST free it with cdsl_free_rule(); fine-grained
 *       cdsl_free_expr() must not be called on it, because the per-node
 *       arena is no longer reachable via the thread-local stack. The arena
 *       is attached to rule->arena on the errors path so that
 *       cdsl_free_rule reclaims it safely.
```

- [ ] **Step 3: 写 test_arena_ownership.c**

构造会 partial-parse 的 DSL（如 `RULE r { WHEN user.age THEN }` 缺表达式的规则），在 `errors` 路径下取到 partial rule，验证：
- (a) `cdsl_free_rule(rule)` 干净退出（ASan 无 leak/UAF）；
- (b) partial rule 为 **NULL**（完全解析失败）的 errors 路径 arena 不泄漏；
- (c) `cdsl_free_expr` 拆 partial rule 节点的路径**已无 caller 触发**（grep 全库确认 + 文档约束到位——**测试里可故意调 `cdsl_free_expr` 在 ASan 下断言崩溃，作为 regression 守卫，但默认路径不调**）。

测试用 `tests/test.h` 微框架：`int main(void) { ... 注册 ... }`，参照 `tests/test_ast.c` 的现有结构。

- [ ] **Step 4: ASan 跑 + 全量回归**

```bash
CMAKE_BUILD_TYPE=Debug CFLAGS="-fsanitize=address -fno-omit-frame-pointer" ccmake build && ctest --output-on-failure -R "test_arena_ownership|test_ast"
```

- [ ] **Step 5: commit**

```bash
git add -A && git commit -m "fix(ast/parse): attach local arena to partial rule on errors path; document ownership contract"
```

---

## Task 3: ② server.c HTTP 413 防御

**Files:**
- Modify: `src/server/server.c` (L716 硬编码 10MB + L739 CL 解析点)
- Create: `tests/test_http_limits.c`

- [ ] **Step 1: 提 10MB 宏 + 替换硬编码**

定位 server.c 顶部（`#include` 之后），加：

```c
#define CDSL_SERVER_MAX_BODY_BYTES  (10u * 1024u * 1024u)
```

L716 现状 `if (cap >= (size_t)10 * 1024 * 1024)` → `if (cap >= CDSL_SERVER_MAX_BODY_BYTES)`。

- [ ] **Step 2: 读循环 413 注入（L739 CL 解析之后）**

定位 server.c L733-742（`if (header_end) { content_len = http_get_content_length(...); }` 之后），插入：

```c
if (content_len > 0 && (size_t)content_len > CDSL_SERVER_MAX_BODY_BYTES) {
    const char* resp_413 =
        "HTTP/1.1 413 Payload Too Large\r\n"
        "Content-Length: 0\r\nConnection: close\r\n\r\n";
    send(client_fd, resp_413, strlen(resp_413), 0);
    CDSL_CLOSE_SOCKET(client_fd);
    free(req_buf);
    return;
}
```

**注意**：`return` 前 `free(req_buf)`（L702 `req_buf = malloc(cap)` 的清理），避免泄漏。沿用现有 400/404 的内联字符串 + `Connection: close` + 无条件 close fd 语义（与 L264/L694 一致）。负数/0/缺 CL 头保持 `content_len <= 0` 短路不变。

- [ ] **Step 3: 写 test_http_limits.c**

起真 server 进程（参照 `tests/test_server.c` 的现有 socket 起法），发 curl/sock 验证：
- 伪造 `Content-Length: 100000000`（> 10MB）→ 断言 413 + 连接 close；
- `Content-Length` == 10MB（== 上限）→ 断言 200（边界钉住）；
- 正常 10MB-1 字节 body → 断言 200；
- `Content-Length: 0` / 负数 / 缺 CL 头 → 断言保持现有短路（200 或 400 视现有语义）。

- [ ] **Step 4: 验证 + grep 确认硬编码清除**

```bash
ccmake build && ctest --output-on-failure -R "test_http_limits|test_server"
grep -n "10 * 1024 * 1024\|10\*1024\*1024" src/server/server.c   # 应返回 0 命中
grep -n "CDSL_SERVER_MAX_BODY_BYTES" src/server/server.c          # 应恰好 2 命中（L716 + 413 判定）
```

- [ ] **Step 5: commit**

```bash
git add -A && git commit -m "fix(server): reject oversized Content-Length with 413; extract CDSL_SERVER_MAX_BODY_BYTES macro"
```

---

## Task 4: ③ manager RCU 契约文档化

**Files:**
- Modify: `include/cdsl/manager.h` (`cdsl_manager_acquire`/`cdsl_manager_release` 函数注释)

- [ ] **Step 1: 补线程安全契约注释**

定位 `include/cdsl/manager.h` 的 `cdsl_manager_acquire` / `cdsl_manager_release` 声明处，补 spec §6 的契约段（**release 可跨线程，reader release 后不得再解引用**，与实际 worker 线程用法一致）：

```c
/**
 * cdsl_manager_acquire / cdsl_manager_release — RCU contract
 *
 *  - acquire is an atomic refcount bump (fetch_add). It MAY be called
 *    from any reader thread.
 *  - release is an atomic refcount drop (fetch_sub) and is thread-safe:
 *    the releasing thread MUST be the one that owns the last use of the
 *    acquired snapshot; the reader MUST NOT dereference the snapshot
 *    after calling release. Cross-thread release is permitted because
 *    the operation is atomic.
 *  - The ruleset is recycled only in the deploy_canary / destroy path,
 *    guarded by manager.c's write_lock (the deploy path's mutex), and
 *    ONLY after ref_count has reached 0 (no live reader). No thread may
 *    free the old ruleset's arena while any reader still holds a ref.
 *  - Violation (freeing a ruleset while ref_count > 0) = use-after-free
 *    on the ruleset's arena.
 */
```

**注意**：契约文本用 `manager.c` 的 `write_lock`（实际字段名），而非 `deploy_mutex`（spec 注释草稿里的措辞，实现中不存在——review round-2 R5 已核实）。

- [ ] **Step 2: 验证（纯文档，无行为变化）**

```bash
ccmake build && ctest --output-on-failure -R "test_manager"
```

`test_manager.c` 回归全绿（文档不改行为）。

- [ ] **Step 3: commit**

```bash
git add include/cdsl/manager.h && git commit -m "docs(manager): document RCU acquire/release cross-thread contract"
```

---

## Task 5: ⑤ CMake no-op 验证 + 全量回归 + python 绑定同步

**Files:**
- Modify: `python/cdsl/__init__.py` (ctypes 结构布局同步 `cdsl_json_value_t` union 扩展)
- Modify: `CMakeLists.txt` (仅 `grep` 验证，**不改**——现状 L216 已含 CONFIGURE_DEPENDS)

- [ ] **Step 1: 同步 python ctypes 布局**

定位 `python/cdsl/__init__.py` 中 `cdsl_json_value_t` 的 ctypes 结构定义，把 number union 的 `double number_val` 字段同步为 `c_double f64` + `c_longlong i64`（union 大小可能变，ctypes 用 Union 类同步）。**注意**：python 绑定若走的是 shared 库 ABI（ctypes `Structure`），union 扩展后 sizeof 变化——需确认 `cdsl_json_value_t` 是否跨 ABI 边界暴露给 python（若仅内部用则 python 侧无需改）。grep python 目录确认是否有该 struct 的 ctypes 定义。

- [ ] **Step 2: 验证 ⑤ no-op（CONFIGURE_DEPENDS 现状已满足）**

```bash
grep -n "CONFIGURE_DEPENDS" CMakeLists.txt   # L85/L216/L333 应已含
ls tests/test_*.c | wc -l                    # 确认新测试文件（test_arena_ownership/test_http_limits/test_json_int64）已被 ctest 自动拉取
ctest -N | grep -E "arena_ownership|http_limits|json_int64"   # 三个新测试应已注册
```

- [ ] **Step 3: 全量回归（Debug + ASan + UBSan 三配置）**

```bash
ccmake build && ctest --output-on-failure          # Debug 全绿
CMAKE_BUILD_TYPE=Debug CFLAGS="-fsanitize=address" ccmake build && ctest --output-on-failure   # ASan 全绿
CMAKE_BUILD_TYPE=Debug CFLAGS="-fsanitize=undefined" ccmake build && ctest --output-on-failure  # UBSan 全绿
```

- [ ] **Step 4: commit**

```bash
git add -A && git commit -m "chore(bindings,cmake): sync python ctypes for json union; verify CONFIGURE_DEPENDS no-op"
```

---

## 全量 DoD 自检（spec §8 7 条）

完成 Task 1-5 后逐条验证：

- [ ] ① `test_arena_ownership` 全绿；ASan 零报错、零 leak
- [ ] ② `test_http_limits` 全绿；curl 伪造大 CL 返回 413；`grep CDSL_SERVER_MAX_BODY_BYTES server.c` 恰好 2 命中
- [ ] ④ `test_json_int64` 全绿：LLONG_MAX → INT64_MAX/INT；LLONG_MAX+1 → FLOAT 降级；`test_json.c` 扩展用例：`2147483648` → LONG、`42` → INT、`9.2` → FLOAT、数组首元素 LONG
- [ ] ⑤ 三个新测试文件被 ctest 自动拉取（CONFIGURE_DEPENDS 现状已满足）
- [ ] ③ `include/cdsl/manager.h` acquire/release 注释含 RCU 契约段（`write_lock` 措辞，非 `deploy_mutex`）
- [ ] 全库 `grep number_val` 0 命中（除 json.h 定义）；`grep "10 * 1024 * 1024" server.c` 0 命中
- [ ] 三配置（Debug/ASan/UBSan）`ctest` 全绿

---

## 明确不做（YAGNI，spec §10）

- 不改 `src/vm/{bytecode.c,threaded.c,jit.c}`（value_ops.c 已含完整 LONG 分支）；
- 不引入 ctest；
- 不做 JIT 逐指令机器码发射；
- 不修 6 项已核实为误判/已解决/现状已满足的项；
- 不给 `cdsl_server_t` 引入 in-flight refcount；
- 不单独建 `test_dummy.c`（⑤ no-op，验证并入新增测试文件的顺带回归）。
