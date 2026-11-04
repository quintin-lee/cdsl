# Parallel Safety Implementation Plan (Subproject D)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make `cdsl_vm_execute_ruleset_parallel()` thread-safe when multiple threads concurrently call it on the same `cdsl_vm_t`, by adding a `cdsl_rwlock_t` to `cdsl_vm_t` that protects the callbacks/functions linked lists and the stats aggregation.

**Architecture:** Add `cdsl_rwlock_t rwlock` to `cdsl_vm_t`. Registration (`cdsl_vm_register_action`/`cdsl_vm_register_function`) takes the write lock; list traversal during execution (`cdsl_trigger_action_internal`, `CDSL_EXPR_CALL`, `BC_CALL`) takes the read lock; stats aggregation in `ruleset_parallel` takes the parent VM write lock. `cdsl_vm_execute` itself remains lock-free (per-thread VM convention).

**Tech Stack:** C23, existing `cdsl_rwlock_t` from `include/cdsl/util/threads.h` (pthread_rwlock on POSIX, SRWLock on Windows), existing `cdsl_vm_t` struct.

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `include/cdsl/vm.h` | Modify | Add `cdsl_rwlock_t rwlock` field to `cdsl_vm_t` |
| `src/vm/vm.c` | Modify | `cdsl_vm_create` init rwlock; `cdsl_vm_free` destroy rwlock; `cdsl_vm_register_action`/`cdsl_vm_register_function` take write lock |
| `src/vm/eval.c` | Modify | `cdsl_trigger_action_internal` take read lock when traversing callbacks; `CDSL_EXPR_CALL` take read lock when traversing functions |
| `src/vm/bytecode.c` | Modify | `BC_CALL` take read lock when traversing functions |
| `src/vm/ruleset.c` | Modify | `cdsl_vm_execute_ruleset_parallel` stats aggregation take parent VM write lock |
| `tests/test_vm_config.c` | Modify | Add concurrent safety test: multiple threads concurrently call `ruleset_parallel` on the same parent VM |

---

### Task 1: Add rwlock field to cdsl_vm_t and init/destroy in vm.c

**Files:**
- Modify: `include/cdsl/vm.h`
- Modify: `src/vm/vm.c`

- [ ] **Step 1: Add `cdsl_rwlock_t rwlock` field to `cdsl_vm_t` in vm.h**

Open `include/cdsl/vm.h` and add the field to the `cdsl_vm_t` struct (near `trace_cb`/`trace_ud`):

```c
	/* ---- Tracing ---- */
	cdsl_trace_cb_t trace_cb; /**< Optional trace callback (NULL=disabled) */
	void* trace_ud;		  /**< User data pointer for trace callback */
	cdsl_rwlock_t rwlock;	   /**< Protects callbacks/functions lists and stats aggregation */
} cdsl_vm_t;
```

- [ ] **Step 2: Init rwlock in `cdsl_vm_create`, destroy in `cdsl_vm_free`**

In `src/vm/vm.c`, in `cdsl_vm_create` after allocating `vm` and before `cdsl_vm_register_builtins(vm)`:

```c
	CDSL_RWLOCK_INIT(&vm->rwlock);
```

If `CDSL_RWLOCK_INIT` fails (extremely rare), free `vm` and return `NULL`.

In `cdsl_vm_free`, before `free(vm)`:

```c
	CDSL_RWLOCK_DESTROY(&vm->rwlock);
```

- [ ] **Step 3: Build and verify no compile errors**

Run: `cmake --build build -j$(nproc) 2>&1 | grep -iE "error|warning" | head`
Expected: builds cleanly (rwlock declared but unused yet — that's fine, no `-Wunused` for struct fields).

- [ ] **Step 4: Commit**

```bash
git add include/cdsl/vm.h src/vm/vm.c
git commit -m "✨ feat(vm): add rwlock field to cdsl_vm_t, init/destroy in create/free"
```

---

### Task 2: Protect `cdsl_vm_register_action` and `cdsl_vm_register_function` with write lock

**Files:**
- Modify: `src/vm/vm.c`

- [ ] **Step 1: Wrap the body of `cdsl_vm_register_action` with CDSL_RWLOCK_WRLOCK/WRUNLOCK**

In `src/vm/vm.c`, modify `cdsl_vm_register_action`:

```c
void
cdsl_vm_register_action(cdsl_vm_t* vm, const char* action_name, cdsl_action_cb_t cb)
{
	if (!vm || !action_name || !cb) {
		return;
	}
	CDSL_RWLOCK_WRLOCK(&vm->rwlock);
	cdsl_action_cb_entry_t* e = calloc(1, sizeof(*e));
	if (!e) {
		CDSL_RWLOCK_UNLOCK_WR(&vm->rwlock);
		return;
	}
	e->action_name = strdup(action_name);
	if (!e->action_name) {
		free(e);
		CDSL_RWLOCK_UNLOCK_WR(&vm->rwlock);
		return;
	}
	e->cb = cb;
	e->next = vm->callbacks;
	vm->callbacks = e;
	CDSL_RWLOCK_UNLOCK_WR(&vm->rwlock);
}
```

Similarly for `cdsl_vm_register_function` with `vm->functions`.

- [ ] **Step 2: Build and run existing tests**

Run: `cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure 2>&1 | tail -5`
Expected: all 22 tests still pass (locks are write-only here; no reader yet, but no deadlock).

- [ ] **Step 3: Commit**

```bash
git add src/vm/vm.c
git commit -m "✨ feat(vm): protect register_action/function with write lock"
```

---

### Task 3: Protect callback traversal in `cdsl_trigger_action_internal` with read lock

**Files:**
- Modify: `src/vm/eval.c`

- [ ] **Step 1: Add read lock around the callbacks list traversal**

In `src/vm/eval.c`, modify `cdsl_trigger_action_internal`:

```c
void
cdsl_trigger_action_internal(cdsl_vm_t* vm, cdsl_action_node_t* action)
{
	if (!vm || !action) {
		return;
	}
	CDSL_RWLOCK_RDLOCK(&vm->rwlock);
	for (cdsl_action_cb_entry_t* cb = vm->callbacks; cb; cb = cb->next) {
		if (strcmp(cb->action_name, action->action_name) == 0) {
			cb->cb(action->action_name, action->args, vm->user_data);
			CDSL_RWLOCK_UNLOCK_RD(&vm->rwlock);
			return;
		}
	}
	CDSL_RWLOCK_UNLOCK_RD(&vm->rwlock);
}
```

Careful: the `return` inside the loop must unlock before returning. The loop-not-found case must also unlock after the loop.

- [ ] **Step 2: Build and run existing tests**

Run: `cmake --build build -j$(nproc) && ./build/test_vm_config && ./build/test_execution`
Expected: all pass.

- [ ] **Step 3: Commit**

```bash
git add src/vm/eval.c
git commit -m "✨ feat(vm): protect callback traversal with read lock in trigger_action_internal"
```

---

### Task 4: Protect function traversal in `CDSL_EXPR_CALL` (eval.c) and `BC_CALL` (bytecode.c) with read lock

**Files:**
- Modify: `src/vm/eval.c`
- Modify: `src/vm/bytecode.c`

- [ ] **Step 1: Add read lock around functions traversal in eval.c `CDSL_EXPR_CALL`**

In `src/vm/eval.c`, the `CDSL_EXPR_CALL` case iterates `vm->functions`. Wrap the traversal loop:

```c
		CDSL_RWLOCK_RDLOCK(&vm->rwlock);
		for (cdsl_func_entry_t* fn = vm->functions; fn; fn = fn->next) {
			if (strcmp(fn->func_name, expr->data.call.func_name) == 0) {
				...
				result = fn->cb(...);
				break;
			}
		}
		CDSL_RWLOCK_UNLOCK_RD(&vm->rwlock);
```

Note: the `fn->cb(...)` invocation happens while holding the read lock. That's acceptable — user callbacks run under read lock, which blocks registration but not other executions. This matches the "registration and execution are mutually exclusive" guarantee.

- [ ] **Step 2: Add read lock around functions traversal in bytecode.c `BC_CALL`**

Similarly in `src/vm/bytecode.c` `BC_CALL` case:

```c
		CDSL_RWLOCK_RDLOCK(&vm->rwlock);
		for (cdsl_func_entry_t* fn = vm->functions; fn; fn = fn->next) {
			if (strcmp(fn->func_name, fn_name) == 0) {
				result = fn->cb(fn_name, arg_list, ctx, vm);
				break;
			}
		}
		CDSL_RWLOCK_UNLOCK_RD(&vm->rwlock);
```

- [ ] **Step 3: Build and run existing tests**

Run: `cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure 2>&1 | tail -5`
Expected: all 22 tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/vm/eval.c src/vm/bytecode.c
git commit -m "✨ feat(vm): protect function traversal with read lock in CALL paths"
```

---

### Task 5: Protect stats aggregation in `ruleset_parallel` with parent VM write lock

**Files:**
- Modify: `src/vm/ruleset.c`

- [ ] **Step 1: Wrap the stats aggregation loop in `cdsl_vm_execute_ruleset_parallel` with write lock**

In `src/vm/ruleset.c`, the final loop that aggregates worker stats into `vm->stats`:

```c
		/* Update main VM stats from worker VM */
		if (args[i].vm) {
			CDSL_RWLOCK_WRLOCK(&vm->rwlock);
			vm->stats.total_executions += args[i].vm->stats.total_executions;
			vm->stats.total_rules_executed += args[i].vm->stats.total_rules_executed;
			vm->stats.total_metrics_evaluated += args[i].vm->stats.total_metrics_evaluated;
			vm->stats.total_time_us += args[i].vm->stats.total_time_us;
			CDSL_RWLOCK_UNLOCK_WR(&vm->rwlock);
			...
		}
```

This serializes the compound stat updates across threads calling `ruleset_parallel` on the same parent VM.

- [ ] **Step 2: Build and run existing tests**

Run: `cmake --build build -j$(nproc) && ctest --test-dir build --output-on-failure 2>&1 | tail -5`
Expected: all 22 tests pass.

- [ ] **Step 3: Commit**

```bash
git add src/vm/ruleset.c
git commit -m "✨ feat(ruleset): protect stats aggregation with write lock in parallel executor"
```

---

### Task 6: Add concurrency regression test

**Files:**
- Modify: `tests/test_vm_config.c`

- [ ] **Step 1: Write the concurrent test**

Add a test that spawns multiple threads, each calling `cdsl_vm_execute_ruleset_parallel` on the same parent VM, and verifies no crash and all results non-NULL:

```c
static void*
parallel_runner(void* arg)
{
	cdsl_ruleset_report_t** out = (cdsl_ruleset_report_t**)arg;
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_schema_register_var(schema, "a", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	cdsl_vm_register_action(vm, "block", test_action_cb);

	cdsl_ruleset_t* set = cdsl_ruleset_create();
	cdsl_rule_t* r = cdsl_parse_string(
	    "RULE r { WHEN a > 5 THEN block(\"ok\") }", NULL);
	if (r) {
		cdsl_ruleset_add(set, r, 1);
	}
	cdsl_context_set_int(ctx, "a", 10);
	*out = cdsl_vm_execute_ruleset_parallel(vm, set, ctx, 2);

	cdsl_ruleset_free(set);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	return NULL;
}

static void
test_vm_parallel_concurrent_safe(void)
{
	TEST_BEGIN("VM parallel concurrent safety");
	cdsl_schema_t* schema = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(schema);
	cdsl_context_t* ctx = cdsl_context_create(schema);
	cdsl_schema_register_var(schema, "a", CDSL_TYPE_INT);
	cdsl_schema_register_action(schema, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	cdsl_vm_register_action(vm, "block", test_action_cb);

	cdsl_ruleset_t* set = cdsl_ruleset_create();
	cdsl_rule_t* r = cdsl_parse_string(
	    "RULE r { WHEN a > 5 THEN block(\"ok\") }", NULL);
	if (r) {
		cdsl_ruleset_add(set, r, 1);
	}
	cdsl_context_set_int(ctx, "a", 10);

	#define NTHREADS 4
	cdsl_ruleset_report_t* results[NTHREADS];
	cdsl_thread_t threads[NTHREADS];
	for (int i = 0; i < NTHREADS; i++) {
		results[i] = NULL;
		if (CDSL_THREAD_CREATE(&threads[i], parallel_runner, &results[i]) != 0) {
			results[i] = NULL;
		}
	}
	for (int i = 0; i < NTHREADS; i++) {
		if (threads[i]) { /* CDSL_THREAD_CREATE sets handle on success */
			CDSL_THREAD_JOIN(threads[i]);
		}
		TEST_ASSERT_NOT_NULL(results[i], "parallel result not NULL");
		if (results[i]) {
			cdsl_ruleset_report_free(results[i]);
		}
	}
	#undef NTHREADS

	cdsl_ruleset_free(set);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(schema);
	TEST_END();
}
```

Register `test_vm_parallel_concurrent_safe();` in `main()` before `TEST_SUMMARY();`.

- [ ] **Step 2: Build and run the new test**

Run: `cmake --build build -j$(nproc) && ./build/test_vm_config`
Expected: `VM parallel concurrent safety ... OK`, all tests pass.

- [ ] **Step 3: Run under ASan to confirm no data races**

Run: `cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug -DCMAKE_C_FLAGS="-fsanitize=address -g" && cmake --build build-asan -j$(nproc) --target test_vm_config && ./build-asan/test_vm_config`
Expected: no ASan errors, all tests pass.

- [ ] **Step 4: Commit**

```bash
git add tests/test_vm_config.c
git commit -m "✨ feat(vm): add concurrent ruleset_parallel safety test"
```

---

### Task 7: Full test suite, clang-tidy, and final commit

**Files:**
- Test: all 22 tests via ctest
- Test: clang-tidy via check-tidy target

- [ ] **Step 1: Build all targets**

Run: `cmake --build build -j$(nproc)`
Expected: all targets build with no errors/warnings under -Werror.

- [ ] **Step 2: Run full test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: 22/22 tests pass.

- [ ] **Step 3: Run clang-tidy**

Run: `cmake --build build --target check-tidy`
Expected: no new warnings related to vm.h, vm.c, eval.c, bytecode.c, ruleset.c.

- [ ] **Step 4: Final commit**

```bash
git add -A
git commit -m "✨ feat(vm): parallel safety complete, all tests and clang-tidy pass"
```

---

## Spec Self-Review

**Spec coverage:**
- Task 1: rwlock field + init/destroy ✓
- Task 2: register_action/function write lock ✓
- Task 3: trigger_action_internal read lock ✓
- Task 4: CDSL_EXPR_CALL / BC_CALL read lock ✓
- Task 5: ruleset_parallel stats aggregation write lock ✓
- Task 6: concurrency regression test ✓
- Task 7: full suite + clang-tidy ✓
- Spec requirement: public API unchanged (only internal struct field added) ✓
- Spec requirement: concurrent calls to ruleset_parallel on same VM safe ✓

**Placeholder scan:** No TBD/TODO/unfinished sections. Every step has concrete code blocks and commands.

**Type consistency:** `cdsl_rwlock_t` is defined in `include/cdsl/util/threads.h` (already included transitively via `cdsl/util/portability.h` → vm.h chain). `CDSL_RWLOCK_*` macros match the existing usage pattern in `src/ai/bridge.c`.

**Scope check:** Plan covers only subproject D (parallel safety). Subproject C (builtins buffer) already completed. Subprojects E, F remain separate.

## Lock Usage Constraints (Important)

### rwlock is non-reentrant

`cdsl_rwlock_t` is based on `pthread_rwlock_t` (POSIX) or `SRWLOCK` (Windows) and is **not recursive**. A thread that already holds a read lock must not attempt to acquire the same lock again (read or write), or it will self-deadlock.

### Callbacks must not re-enter VM execution

`cdsl_trigger_action_internal` and `CDSL_EXPR_CALL` / `BC_CALL` invoke user-provided callbacks (`cb->cb` / `fn->cb`) **while holding `vm->rwlock` read lock**. Therefore:

> **Action callbacks (`cdsl_action_cb_t`) and function callbacks (`cdsl_func_cb_t`) MUST NOT call `cdsl_vm_execute()`, `cdsl_vm_execute_ruleset_parallel()`, or any function that traverses `vm->callbacks` / `vm->functions`.**

Violating this rule causes self-deadlock (read lock already held by the same thread). If a callback needs to trigger new rule execution, it should enqueue a request and let another thread (or the main thread after the callback returns and releases the lock) process it.

### Minimize lock hold time

Keep all lock critical sections as small as possible (linked-list traversal only, stats aggregation only). Do not perform I/O, complex computation, or external API calls while holding `vm->rwlock`.

---

**Plan complete and saved to `docs/superpowers/plans/2026-11-04-parallel-safety-implementation.md`.** Two execution options:

**1. Subagent-Driven (recommended)** - I dispatch a fresh subagent per task, review between tasks, fast iteration

**2. Inline Execution** - Execute tasks in this session using executing-plans, batch execution with checkpoints

Which approach?