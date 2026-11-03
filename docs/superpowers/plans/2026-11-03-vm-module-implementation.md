# VM Module Implementation Plan (Subproject A)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create `src/vm/vm.c` containing all VM lifecycle/configuration functions migrated from `src/vm/context.c`, leaving `context.c` with only context-variable functions. Public API (`cdsl/vm.h`) unchanged.

**Architecture:** New `vm.c` owns VM object lifecycle (`cdsl_vm_create`/`cdsl_vm_free`), action/function registration, debug, expression-depth limits, statistics, sandbox quotas (timeout/memory/instruction), and trace callbacks. `context.c` retains only `cdsl_context_*` variable-binding functions. `internal.h` unchanged — `cdsl_vm_register_builtins` declared there, called by `cdsl_vm_create` in `vm.c`.

**Tech Stack:** C23, CMake, Flex/Bison (parser), pthreads (parallel ruleset, unaffected), existing `cdsl_vm_t` struct from `cdsl/vm.h`.

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `src/vm/vm.c` | Create | All VM lifecycle/config functions (16 functions migrated from context.c) |
| `src/vm/context.c` | Modify | Remove VM lifecycle functions; keep only `cdsl_context_*` variable functions |
| `tests/test_vm_config.c` | Modify | Add tests for VM lifecycle functions (create/free/register/quotas/trace) |
| `src/vm/internal.h` | No change | `cdsl_vm_register_builtins` stays declared here |

---

### Task 1: Create vm.c with cdsl_vm_create

**Files:**
- Create: `src/vm/vm.c`
- Modify: `tests/test_vm_config.c`

- [ ] **Step 1: Write the failing test**

Add a test to `tests/test_vm_config.c` that creates a VM and verifies it is non-NULL:

```c
static void
test_vm_create_returns_non_null(void)
{
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_assert(vm != NULL, "cdsl_vm_create should return non-NULL");
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
}
```

Add to test main/registration list:

```c
cdsl_test_register("vm_create_returns_non_null", test_vm_create_returns_non_null);
```

Run: `cmake --build build -j$(nproc) && ./build/test_vm_config`
Expected: compile error — `cdsl_vm_create` not yet declared/defined in vm.c context (linker error if header not included, or undefined reference).

- [ ] **Step 2: Create src/vm/vm.c with cdsl_vm_create**

Create `src/vm/vm.c`:

```c
/**
 * @file vm.c
 * @brief Virtual Machine lifecycle, configuration, and sandboxing.
 *
 * Implements cdsl_vm_create/free, action/function registration,
 * debug mode, expression-depth limits, statistics, sandbox quotas
 * (timeout/memory/instruction), and trace callbacks.
 *
 * @defgroup cdsl_vm Virtual Machine Lifecycle
 * @{
 */

#include "cdsl/execution.h"
#include "internal.h"
#include <stdlib.h>

cdsl_vm_t*
cdsl_vm_create(const cdsl_schema_t* schema)
{
	cdsl_vm_t* vm = calloc(1, sizeof(*vm));
	if (!vm) {
		return NULL;
	}
	vm->schema = schema;
	vm->debug_enabled = 0;
	vm->max_expr_depth = CDSL_MAX_EXPR_DEPTH;
	cdsl_vm_register_builtins(vm);
	return vm;
}

/** @} */
```

- [ ] **Step 3: Run test to verify it passes**

Run: `cmake --build build -j$(nproc) && ./build/test_vm_config`
Expected: test_vm_config runs, `vm_create_returns_non_null` passes.

- [ ] **Step 4: Commit**

```bash
git add src/vm/vm.c tests/test_vm_config.c
git commit -m "✨ feat(vm): create vm.c with cdsl_vm_create"
```

---

### Task 2: Migrate cdsl_vm_free to vm.c

**Files:**
- Modify: `src/vm/vm.c`
- Modify: `src/vm/context.c` (lines ~670-693)

- [ ] **Step 1: Write the failing test**

Add test to `tests/test_vm_config.c`:

```c
static void
test_vm_free_null_safe(void)
{
	cdsl_vm_free(NULL); /* should not crash */
	cdsl_assert(1, "cdsl_vm_free(NULL) should not crash");
}
```

Register: `cdsl_test_register("vm_free_null_safe", test_vm_free_null_safe);`

Run: `./build/test_vm_config`
Expected: compiles (cdsl_vm_free already declared in vm.h), but linking may fail if not yet defined in vm.c — actually cdsl_vm_free is currently in context.c, so it links. After we remove it from context.c in Step 3, it will fail until vm.c defines it. To keep TDD order, we can add the test now (it passes with context.c's version), then remove from context.c in Step 3, then add to vm.c.

Actually simpler: write test now, it passes with current context.c implementation.

- [ ] **Step 2: Add cdsl_vm_free to vm.c**

Append to `src/vm/vm.c`:

```c
void
cdsl_vm_free(cdsl_vm_t* vm)
{
	if (!vm) {
		return;
	}
	cdsl_action_cb_entry_t* cb = vm->callbacks;
	while (cb) {
		cdsl_action_cb_entry_t* next = cb->next;
		free(cb->action_name);
		free(cb);
		cb = next;
	}
	cdsl_func_entry_t* fn = vm->functions;
	while (fn) {
		cdsl_func_entry_t* next = fn->next;
		free(fn->func_name);
		free(fn);
		fn = next;
	}
	free(vm);
}
```

- [ ] **Step 3: Remove cdsl_vm_free from context.c**

Remove the `cdsl_vm_free` function definition from `src/vm/context.c` (the block from `void cdsl_vm_free(cdsl_vm_t* vm)` through its closing `}` before `cdsl_vm_register_action`).

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build -j$(nproc) && ./build/test_vm_config`
Expected: all tests pass, including `vm_free_null_safe`.

- [ ] **Step 5: Commit**

```bash
git add src/vm/vm.c src/vm/context.c tests/test_vm_config.c
git commit -m "✨ feat(vm): migrate cdsl_vm_free to vm.c"
```

---

### Task 3: Migrate cdsl_vm_register_action and cdsl_vm_register_function to vm.c

**Files:**
- Modify: `src/vm/vm.c`
- Modify: `src/vm/context.c` (lines ~693-730)

- [ ] **Step 1: Write the failing test**

Add to `tests/test_vm_config.c`:

```c
static void
test_vm_register_action_and_function(void)
{
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);

	int called = 0;
	void my_action(const char* name, cdsl_arg_node_t* args, void* ud)
	{
		(void)name; (void)args; (void)ud;
		called = 1;
	}
	cdsl_vm_register_action(vm, "test_action", my_action);
	cdsl_vm_register_function(vm, "test_func", NULL);

	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	cdsl_assert(1, "registration should not crash");
}
```

Register and run: should pass with current context.c implementation.

- [ ] **Step 2: Add both functions to vm.c**

Append to `src/vm/vm.c`:

```c
void
cdsl_vm_register_action(cdsl_vm_t* vm, const char* action_name, cdsl_action_cb_t cb)
{
	if (!vm || !action_name || !cb) {
		return;
	}
	cdsl_action_cb_entry_t* e = calloc(1, sizeof(*e));
	if (!e) {
		return;
	}
	e->action_name = strdup(action_name);
	if (!e->action_name) {
		free(e);
		return;
	}
	e->cb = cb;
	e->next = vm->callbacks;
	vm->callbacks = e;
}

void
cdsl_vm_register_function(cdsl_vm_t* vm, const char* func_name, cdsl_func_cb_t cb)
{
	if (!vm || !func_name || !cb) {
		return;
	}
	cdsl_func_entry_t* e = calloc(1, sizeof(*e));
	if (!e) {
		return;
	}
	e->func_name = strdup(func_name);
	if (!e->func_name) {
		free(e);
		return;
	}
	e->cb = cb;
	e->next = vm->functions;
	vm->functions = e;
}
```

- [ ] **Step 3: Remove both functions from context.c**

Remove `cdsl_vm_register_action` and `cdsl_vm_register_function` definitions from `src/vm/context.c` (from `void cdsl_vm_register_action(` through end of file).

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build -j$(nproc) && ./build/test_vm_config`
Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/vm/vm.c src/vm/context.c tests/test_vm_config.c
git commit -m "✨ feat(vm): migrate cdsl_vm_register_action/function to vm.c"
```

---

### Task 4: Migrate cdsl_vm_set_debug and get/set_max_expr_depth to vm.c

**Files:**
- Modify: `src/vm/vm.c`
- Modify: `src/vm/context.c` (lines ~562-590)

- [ ] **Step 1: Write the failing test**

Add to `tests/test_vm_config.c`:

```c
static void
test_vm_debug_and_depth(void)
{
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);

	cdsl_vm_set_debug(vm, 1);
	cdsl_assert(cdsl_vm_get_max_expr_depth(vm) == CDSL_MAX_EXPR_DEPTH,
		    "default max expr depth should be CDSL_MAX_EXPR_DEPTH");

	cdsl_vm_set_max_expr_depth(vm, 32);
	cdsl_assert(cdsl_vm_get_max_expr_depth(vm) == 32, "max expr depth should be 32");

	cdsl_vm_set_max_expr_depth(vm, 0); /* invalid, should be ignored */
	cdsl_assert(cdsl_vm_get_max_expr_depth(vm) == 32, "depth 0 should be ignored");

	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
}
```

Register and run: passes with current context.c.

- [ ] **Step 2: Add four functions to vm.c**

Append to `src/vm/vm.c`:

```c
void
cdsl_vm_set_debug(cdsl_vm_t* vm, int enabled)
{
	if (vm) {
		vm->debug_enabled = enabled;
	}
}

int
cdsl_vm_get_max_expr_depth(const cdsl_vm_t* vm)
{
	return vm ? vm->max_expr_depth : CDSL_MAX_EXPR_DEPTH;
}

void
cdsl_vm_set_max_expr_depth(cdsl_vm_t* vm, int depth)
{
	if (vm && depth > 0) {
		vm->max_expr_depth = depth;
	}
}
```

(`cdsl_vm_set_debug` already covered; `cdsl_vm_get_max_expr_depth`/`cdsl_vm_set_max_expr_depth` added here.)

- [ ] **Step 3: Remove from context.c**

Remove `cdsl_vm_set_debug`, `cdsl_vm_get_max_expr_depth`, `cdsl_vm_set_max_expr_depth` from `src/vm/context.c`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build -j$(nproc) && ./build/test_vm_config`
Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/vm/vm.c src/vm/context.c tests/test_vm_config.c
git commit -m "✨ feat(vm): migrate debug and max_expr_depth to vm.c"
```

---

### Task 5: Migrate cdsl_vm_get_stats and cdsl_vm_reset_stats to vm.c

**Files:**
- Modify: `src/vm/vm.c`
- Modify: `src/vm/context.c` (lines ~584-618)

- [ ] **Step 1: Write the failing test**

Add to `tests/test_vm_config.c`:

```c
static void
test_vm_stats_reset(void)
{
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);

	cdsl_stats_t* s = cdsl_vm_get_stats(vm);
	cdsl_assert(s != NULL, "get_stats should return non-NULL");
	cdsl_assert(s->total_executions == 0, "executions should start at 0");
	free(s);

	cdsl_vm_reset_stats(vm);
	cdsl_assert(1, "reset_stats should not crash");

	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
}
```

Register and run: passes with current context.c.

- [ ] **Step 2: Add both functions to vm.c**

Append to `src/vm/vm.c`:

```c
cdsl_stats_t*
cdsl_vm_get_stats(const cdsl_vm_t* vm)
{
	if (!vm) {
		return NULL;
	}
	cdsl_stats_t* s = malloc(sizeof(cdsl_stats_t));
	if (!s) {
		return NULL;
	}
	s->total_executions = CDSL_ATOMIC_LOAD(&vm->stats.total_executions);
	s->total_rules_executed = CDSL_ATOMIC_LOAD(&vm->stats.total_rules_executed);
	s->total_metrics_evaluated = CDSL_ATOMIC_LOAD(&vm->stats.total_metrics_evaluated);
	s->total_actions_triggered = CDSL_ATOMIC_LOAD(&vm->stats.total_actions_triggered);
	s->total_time_us = vm->stats.total_time_us;
	s->avg_time_us = vm->stats.avg_time_us;
	if (s->total_executions > 0) {
		s->avg_time_us = s->total_time_us / (double)s->total_executions;
	}
	return s;
}

void
cdsl_vm_reset_stats(cdsl_vm_t* vm)
{
	if (vm) {
		CDSL_ATOMIC_STORE(&vm->stats.total_executions, 0);
		CDSL_ATOMIC_STORE(&vm->stats.total_rules_executed, 0);
		CDSL_ATOMIC_STORE(&vm->stats.total_metrics_evaluated, 0);
		CDSL_ATOMIC_STORE(&vm->stats.total_actions_triggered, 0);
		vm->stats.total_time_us = 0;
		vm->stats.avg_time_us = 0;
	}
}
```

- [ ] **Step 3: Remove from context.c**

Remove `cdsl_vm_get_stats` and `cdsl_vm_reset_stats` from `src/vm/context.c`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build -j$(nproc) && ./build/test_vm_config`
Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/vm/vm.c src/vm/context.c tests/test_vm_config.c
git commit -m "✨ feat(vm): migrate get_stats/reset_stats to vm.c"
```

---

### Task 6: Migrate cdsl_vm_set/get_timeout, memory_limit, instruction_limit to vm.c

**Files:**
- Modify: `src/vm/vm.c`
- Modify: `src/vm/context.c` (lines ~619-664)

- [ ] **Step 1: Write the failing test**

Add to `tests/test_vm_config.c`:

```c
static void
test_vm_quotas(void)
{
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);

	cdsl_vm_set_timeout(vm, 1000000);
	cdsl_assert(cdsl_vm_get_timeout(vm) == 1000000, "timeout should be 1000000");

	cdsl_vm_set_memory_limit(vm, 65536);
	cdsl_assert(cdsl_vm_get_memory_limit(vm) == 65536, "memory limit should be 65536");

	cdsl_vm_set_instruction_limit(vm, 1000);
	cdsl_assert(cdsl_vm_get_instruction_limit(vm) == 1000,
		    "instruction limit should be 1000");

	/* NULL-safe getters */
	cdsl_assert(cdsl_vm_get_timeout(NULL) == 0, "get_timeout(NULL) == 0");
	cdsl_assert(cdsl_vm_get_memory_limit(NULL) == 0,
		    "get_memory_limit(NULL) == 0");
	cdsl_assert(cdsl_vm_get_instruction_limit(NULL) == 0,
		    "get_instruction_limit(NULL) == 0");

	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
}
```

Register and run: passes with current context.c.

- [ ] **Step 2: Add six functions to vm.c**

Append to `src/vm/vm.c`:

```c
void
cdsl_vm_set_timeout(cdsl_vm_t* vm, int64_t timeout_us)
{
	if (vm) {
		vm->timeout_us = timeout_us;
	}
}

int64_t
cdsl_vm_get_timeout(const cdsl_vm_t* vm)
{
	return vm ? vm->timeout_us : 0;
}

void
cdsl_vm_set_memory_limit(cdsl_vm_t* vm, int64_t limit_bytes)
{
	if (vm) {
		vm->memory_limit = limit_bytes;
	}
}

int64_t
cdsl_vm_get_memory_limit(const cdsl_vm_t* vm)
{
	return vm ? vm->memory_limit : 0;
}

void
cdsl_vm_set_instruction_limit(cdsl_vm_t* vm, int64_t limit)
{
	if (vm) {
		vm->instruction_limit = limit;
	}
}

int64_t
cdsl_vm_get_instruction_limit(const cdsl_vm_t* vm)
{
	return vm ? vm->instruction_limit : 0;
}
```

- [ ] **Step 3: Remove from context.c**

Remove `cdsl_vm_set_timeout`, `cdsl_vm_get_timeout`, `cdsl_vm_set_memory_limit`, `cdsl_vm_get_memory_limit`, `cdsl_vm_set_instruction_limit`, `cdsl_vm_get_instruction_limit` from `src/vm/context.c`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build -j$(nproc) && ./build/test_vm_config`
Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/vm/vm.c src/vm/context.c tests/test_vm_config.c
git commit -m "✨ feat(vm): migrate sandbox quota getters/setters to vm.c"
```

---

### Task 7: Migrate cdsl_vm_set_trace_callback to vm.c

**Files:**
- Modify: `src/vm/vm.c`
- Modify: `src/vm/context.c` (line ~619 area)

- [ ] **Step 1: Write the failing test**

Add to `tests/test_vm_config.c`:

```c
static void
test_vm_trace_callback(void)
{
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);

	int called = 0;
	void my_trace(const cdsl_trace_event_t* ev, void* ud)
	{
		(void)ev;
		*(int*)ud = 1;
	}
	cdsl_vm_set_trace_callback(vm, my_trace, &called);
	cdsl_vm_set_trace_callback(vm, NULL, NULL); /* disable */
	cdsl_assert(called == 0, "callback should not have been invoked (just registered)");

	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
}
```

Register and run: passes with current context.c.

- [ ] **Step 2: Add cdsl_vm_set_trace_callback to vm.c**

Append to `src/vm/vm.c`:

```c
void
cdsl_vm_set_trace_callback(cdsl_vm_t* vm, cdsl_trace_cb_t cb, void* user_data)
{
	if (vm) {
		vm->trace_cb = cb;
		vm->trace_ud = user_data;
	}
}
```

- [ ] **Step 3: Remove from context.c**

Remove `cdsl_vm_set_trace_callback` from `src/vm/context.c`.

- [ ] **Step 4: Run test to verify it passes**

Run: `cmake --build build -j$(nproc) && ./build/test_vm_config`
Expected: all tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/vm/vm.c src/vm/context.c tests/test_vm_config.c
git commit -m "✨ feat(vm): migrate cdsl_vm_set_trace_callback to vm.c"
```

---

### Task 8: Verify context.c contains only context functions, run full test suite

**Files:**
- Modify: `src/vm/context.c` (verify no VM functions remain)
- Test: `tests/test_vm_config.c` and all tests

- [ ] **Step 1: Verify context.c has no remaining VM functions**

Run: `grep -n "cdsl_vm_" src/vm/context.c`
Expected: no output (or only references in comments).

Expected remaining functions in context.c: `cdsl_context_get_entry_internal`, `cdsl_context_create`, `cdsl_value_free`, `cdsl_context_free`, `cdsl_context_set_*`, `cdsl_context_get_*`, `cdsl_context_remove`, `cdsl_context_load_json`.

- [ ] **Step 2: Run full test suite**

Run: `cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure`
Expected: all 22+ tests pass, no failures.

- [ ] **Step 3: Run clang-tidy check**

Run: `cmake --build build --target check-tidy` (if clang-tidy available)
Expected: no new warnings related to vm.c or context.c.

- [ ] **Step 4: Final commit**

```bash
git add src/vm/context.c
git commit -m "✨ feat(vm): verify context.c clean, all tests pass"
```

---

## Spec Self-Review

**Spec coverage:** Each of the 16 migrated functions has a corresponding task:
1. `cdsl_vm_create` — Task 1
2. `cdsl_vm_free` — Task 2
3. `cdsl_vm_register_action` — Task 3
4. `cdsl_vm_register_function` — Task 3
5. `cdsl_vm_set_debug` — Task 4
6. `cdsl_vm_get_max_expr_depth` — Task 4
7. `cdsl_vm_set_max_expr_depth` — Task 4
8. `cdsl_vm_get_stats` — Task 5
9. `cdsl_vm_reset_stats` — Task 5
10. `cdsl_vm_set_timeout` — Task 6
11. `cdsl_vm_get_timeout` — Task 6
12. `cdsl_vm_set_memory_limit` — Task 6
13. `cdsl_vm_get_memory_limit` — Task 6
14. `cdsl_vm_set_instruction_limit` — Task 6
15. `cdsl_vm_get_instruction_limit` — Task 6
16. `cdsl_vm_set_trace_callback` — Task 7

**Placeholder scan:** No TBD/TODO/unfinished sections. Every step has concrete code blocks and commands.

**Type consistency:** All function signatures match `cdsl/vm.h` declarations exactly. `cdsl_vm_t`, `cdsl_schema_t`, `cdsl_stats_t`, `cdsl_trace_cb_t`, `cdsl_trace_event_t`, `cdsl_action_cb_t`, `cdsl_func_cb_t` all from existing headers.

**Scope check:** Plan covers only subproject A (vm.c creation). Other 5 subprojects (bytecode dedup, builtins buffer, parallel race, parser error recovery, test coverage) are separate plans.

---

Plan complete and saved to `docs/superpowers/plans/2026-11-03-vm-module-implementation.md`. Two execution options:

**1. Subagent-Driven (recommended)** - I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints

Which approach?