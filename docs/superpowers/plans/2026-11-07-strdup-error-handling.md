# String Duplication (`strdup`) Error Handling & Exception Safety Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish complete NULL/OOM exception safety across all ~60 string duplication sites in C-DSL by introducing `cdsl_strdup()`, transactional rollbacks in independent structures (Context, Schema, Hashmap, VM registries), and unified `CDSL_ERR_RESOURCE_LIMIT` propagation in evaluation and bytecode execution engines.

**Architecture:** A lightweight inline utility `cdsl_strdup(s)` provides NULL-tolerant allocation and integrates a deterministic test-time OOM fault injection hook. Independent subsystems adhere to strong exception safety by rolling back partial allocations upon OOM without corrupting data structures, while VM execution layers trap OOM and return `CDSL_STATUS_ERROR` reports with clean resource reclamation verified under ASan and LSan.

**Tech Stack:** C23, GCC / Clang, CMake, CTest, AddressSanitizer (ASan), LeakSanitizer (LSan), UndefinedBehaviorSanitizer (UBSan).

---

### Task 1: Core `cdsl_strdup` Utility and Fault Injection Hook

**Files:**
- Modify: `include/cdsl/util/portability.h:95-105`
- Modify: `src/vm/internal.h:70-90`
- Create / Modify: `src/util/error.c:15-35` (or a dedicated helper in `src/util/arena.c`)
- Test: `tests/test_memory.c`

- [x] **Step 1: Write the failing test for `cdsl_strdup` and OOM countdown hook**

In `tests/test_memory.c`:
```c
void
test_cdsl_strdup_and_oom_hook(void)
{
	TEST_BEGIN("cdsl_strdup NULL tolerance and OOM injection hook");

	/* Test NULL tolerance */
	char* s_null = cdsl_strdup(NULL);
	TEST_ASSERT_NULL(s_null, "cdsl_strdup(NULL) must return NULL");

	/* Test normal duplication */
	char* s_dup = cdsl_strdup("hello cdsl");
	TEST_ASSERT_NOT_NULL(s_dup, "cdsl_strdup valid string must succeed");
	TEST_ASSERT_STR(s_dup, "hello cdsl", "Duplicated string must match");
	free(s_dup);

	/* Test fault injection hook */
	cdsl_test_set_oom_countdown(1); /* 1st call succeeds, 2nd call fails */
	char* s1 = cdsl_strdup("first");
	TEST_ASSERT_NOT_NULL(s1, "1st call before countdown reaches 0 must succeed");
	free(s1);

	char* s2 = cdsl_strdup("second");
	TEST_ASSERT_NULL(s2, "2nd call when countdown hits 0 must return NULL");

	cdsl_test_set_oom_countdown(-1); /* reset hook */
	char* s3 = cdsl_strdup("third");
	TEST_ASSERT_NOT_NULL(s3, "after hook reset, duplication must succeed");
	free(s3);

	TEST_END();
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_memory && ./build/test_memory`
Expected: Compilation failure or undefined reference to `cdsl_test_set_oom_countdown` / `cdsl_strdup`.

- [x] **Step 3: Implement `cdsl_strdup` and OOM hook**

In `include/cdsl/util/portability.h`:
```c
#ifdef __cplusplus
extern "C" {
#endif

#if defined(CDSL_TESTING) || !defined(NDEBUG)
void cdsl_test_set_oom_countdown(int countdown);
int cdsl_test_get_oom_countdown(void);
int cdsl_test_oom_check_and_decrement(void);
#endif

/**
 * @brief Duplicate a null-terminated string with NULL and allocation safety.
 * @param s String to duplicate (may be NULL)
 * @return Newly heap-allocated duplicate string, or NULL if input is NULL or malloc fails.
 */
static inline char*
cdsl_strdup(const char* s)
{
#if defined(CDSL_TESTING) || !defined(NDEBUG)
	if (cdsl_test_oom_check_and_decrement()) {
		return NULL;
	}
#endif
	if (!s) {
		return NULL;
	}
	return strdup(s);
}

#ifdef __cplusplus
}
#endif
```

In `src/util/error.c` (implement the hook state):
```c
#if defined(CDSL_TESTING) || !defined(NDEBUG)
static cdsl_atomic_int_t g_oom_countdown = -1;

void
cdsl_test_set_oom_countdown(int countdown)
{
	CDSL_ATOMIC_STORE(&g_oom_countdown, countdown);
}

int
cdsl_test_get_oom_countdown(void)
{
	return CDSL_ATOMIC_LOAD(&g_oom_countdown);
}

int
cdsl_test_oom_check_and_decrement(void)
{
	int cd = CDSL_ATOMIC_LOAD(&g_oom_countdown);
	if (cd < 0) {
		return 0;
	}
	if (cd == 0) {
		return 1; /* OOM triggered */
	}
	CDSL_ATOMIC_STORE(&g_oom_countdown, cd - 1);
	return 0;
}
#endif
```

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_memory && ./build/test_memory`
Expected: `[TEST] cdsl_strdup NULL tolerance and OOM injection hook ... OK`

- [x] **Step 5: Commit**

```bash
git add include/cdsl/util/portability.h src/util/error.c tests/test_memory.c
git commit -m "feat(util): ✨ introduce cdsl_strdup and test-time OOM fault injection hook"
```

---

### Task 2: Context Module Transactional Rollback

**Files:**
- Modify: `src/vm/context.c:190-380` (getters, setters, JSON loading)
- Test: `tests/test_context.c`

- [x] **Step 1: Write the failing test for Context OOM rollback**

In `tests/test_context.c`:
```c
static void
test_context_set_string_oom_rollback(void)
{
	TEST_BEGIN("context set string OOM transactional rollback");

	cdsl_schema_t* s = cdsl_schema_create();
	cdsl_schema_register_var(s, "user.name", CDSL_TYPE_STRING);
	cdsl_context_t* ctx = cdsl_context_create(s);

	/* Set initial value */
	int ok = cdsl_context_set_string(ctx, "user.name", "Initial");
	TEST_ASSERT_INT(ok, 1, "Initial set_string succeeds");
	TEST_ASSERT_STR(cdsl_context_get_string(ctx, "user.name", ""), "Initial", "Initial value matches");

	/* Simulate OOM when modifying existing value: old value must be preserved */
	cdsl_test_set_oom_countdown(0); /* force next allocation to fail */
	ok = cdsl_context_set_string(ctx, "user.name", "Modified");
	TEST_ASSERT_INT(ok, 0, "set_string on OOM returns 0");
	TEST_ASSERT_STR(cdsl_context_get_string(ctx, "user.name", ""), "Initial", "Old value preserved on OOM");

	/* Reset hook and add new key under OOM */
	cdsl_test_set_oom_countdown(-1);
	cdsl_test_set_oom_countdown(0);
	ok = cdsl_context_set_string(ctx, "user.new_key", "NewVal");
	TEST_ASSERT_INT(ok, 0, "new key set_string on OOM returns 0");
	TEST_ASSERT_NULL(cdsl_context_get_entry_internal(ctx, "user.new_key"), "No corrupted node inserted");

	cdsl_test_set_oom_countdown(-1);
	cdsl_context_free(ctx);
	cdsl_schema_free(s);
	TEST_END();
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_context && ./build/test_context`
Expected: FAIL on `TEST_ASSERT_STR(..., "Initial", ...)` because old code freed old value before `strdup` succeeded.

- [x] **Step 3: Implement transactional rollback in `src/vm/context.c`**

Replace all raw `strdup` calls with `cdsl_strdup` and add rollback handling:
1. In `cdsl_context_set_string`:
   - Allocate `char* new_val = val ? cdsl_strdup(val) : NULL;`
   - If `val && !new_val`, return `0` immediately without touching `e`.
   - If `e` exists: free old `e->value.data.string_val`, assign `new_val`, return `1`.
   - If `!e`: allocate `char* new_name = cdsl_strdup(name)`. If `!new_name`, free `new_val`, return `0`.
   - Allocate `cdsl_context_entry_t* ne = calloc(1, sizeof(*ne))`. If `!ne`, free `new_name`, free `new_val`, return `0`.
   - Assign and link `ne` to `ctx->entries`.
2. In `cdsl_context_set_int/float/bool/date/long`:
   - For new entries: `char* new_name = cdsl_strdup(name);` if `!new_name`, return `0`.
   - Allocate `ne`; if `!ne`, free `new_name` and return `0`.
3. In `cdsl_context_load_json`:
   - When copying JSON keys and strings, check `cdsl_strdup` return values. On NULL, free temporary key buffers and return `0`.

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_context && ./build/test_context`
Expected: PASS with 0 failures.

- [x] **Step 5: Commit**

```bash
git add src/vm/context.c tests/test_context.c
git commit -m "fix(context): 🐛 ensure transactional rollback on strdup failure in context setters"
```

---

### Task 3: Schema and Hashmap Module Rollback

**Files:**
- Modify: `src/schema/schema.c:70-130`
- Modify: `src/util/hashmap.c:120-145`
- Test: `tests/test_schema.c`

- [x] **Step 1: Write the failing test for Schema registration OOM**

In `tests/test_schema.c`:
```c
static void
test_schema_registration_oom(void)
{
	TEST_BEGIN("schema register var and action OOM safety");

	cdsl_schema_t* s = cdsl_schema_create();

	/* Simulate OOM during register_var */
	cdsl_test_set_oom_countdown(0);
	int ok = cdsl_schema_register_var(s, "user.age", CDSL_TYPE_INT);
	TEST_ASSERT_INT(ok, 0, "register_var on OOM returns 0");
	TEST_ASSERT_NULL(s->vars, "No half-allocated var node in schema");

	/* Simulate OOM during register_action */
	cdsl_test_set_oom_countdown(-1);
	cdsl_test_set_oom_countdown(0);
	ok = cdsl_schema_register_action(s, "block", CDSL_TYPE_VOID, 1, CDSL_TYPE_STRING);
	TEST_ASSERT_INT(ok, 0, "register_action on OOM returns 0");
	TEST_ASSERT_NULL(s->actions, "No half-allocated action node in schema");

	cdsl_test_set_oom_countdown(-1);
	cdsl_schema_free(s);
	TEST_END();
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_schema && ./build/test_schema`
Expected: FAIL because `cdsl_schema_register_var` does not check `strdup` return value.

- [x] **Step 3: Implement clean rollback in `src/schema/schema.c` and `src/util/hashmap.c`**

In `src/schema/schema.c`:
- In `cdsl_schema_register_var`:
  ```c
  char* vname = cdsl_strdup(name);
  if (!vname) {
      free(v);
      return 0;
  }
  v->name = vname;
  ```
- In `cdsl_schema_register_action`:
  ```c
  char* aname = cdsl_strdup(name);
  if (!aname) {
      free(a->param_types);
      free(a);
      return 0;
  }
  a->name = aname;
  ```

In `src/util/hashmap.c`:
- In `cdsl_hashmap_put`:
  ```c
  char* ekey = cdsl_strdup(key);
  if (!ekey) {
      free(e);
      return 0;
  }
  e->key = ekey;
  ```

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_schema && ./build/test_schema`
Expected: PASS with 0 failures.

- [x] **Step 5: Commit**

```bash
git add src/schema/schema.c src/util/hashmap.c tests/test_schema.c
git commit -m "fix(schema): 🐛 clean rollback on strdup failure in schema and hashmap"
```

---

### Task 4: VM Registries Rollback

**Files:**
- Modify: `src/vm/vm.c:65-115`
- Test: `tests/test_vm_config.c`

- [x] **Step 1: Write the failing test for VM callback registration OOM**

In `tests/test_vm_config.c`:
```c
static void
test_vm_register_callbacks_oom(void)
{
	TEST_BEGIN("vm register action and function OOM safety");

	cdsl_schema_t* s = cdsl_schema_create();
	cdsl_vm_t* vm = cdsl_vm_create(s);

	/* Simulate OOM on action registration */
	cdsl_test_set_oom_countdown(0);
	int ok = cdsl_vm_register_action(vm, "block", NULL);
	TEST_ASSERT_INT(ok, 0, "register action on OOM returns 0");
	TEST_ASSERT_NULL(vm->callbacks, "vm->callbacks remains clean");

	/* Simulate OOM on func registration */
	cdsl_test_set_oom_countdown(-1);
	cdsl_test_set_oom_countdown(0);
	ok = cdsl_vm_register_function(vm, "my_fn", NULL);
	TEST_ASSERT_INT(ok, 0, "register func on OOM returns 0");
	TEST_ASSERT_NULL(vm->custom_funcs, "vm->custom_funcs remains clean");

	cdsl_test_set_oom_countdown(-1);
	cdsl_vm_free(vm);
	cdsl_schema_free(s);
	TEST_END();
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_vm_config && ./build/test_vm_config`
Expected: FAIL because `cdsl_vm_register_action` does not check `strdup` and returns void / void*.

- [x] **Step 3: Implement clean rollback in `src/vm/vm.c`**

In `src/vm/vm.c`:
- In `cdsl_vm_register_action`:
  ```c
  char* name_dup = cdsl_strdup(action_name);
  if (!name_dup) {
      free(e);
      CDSL_RWLOCK_UNLOCK_WR(&vm->rwlock);
      return 0;
  }
  e->action_name = name_dup;
  ```
- In `cdsl_vm_register_function`:
  ```c
  char* func_dup = cdsl_strdup(func_name);
  if (!func_dup) {
      free(e);
      CDSL_RWLOCK_UNLOCK_WR(&vm->rwlock);
      return 0;
  }
  e->func_name = func_dup;
  ```

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_vm_config && ./build/test_vm_config`
Expected: PASS with 0 failures.

- [x] **Step 5: Commit**

```bash
git add src/vm/vm.c tests/test_vm_config.c
git commit -m "fix(vm): 🐛 handle strdup failure in action and function registrations"
```

---

### Task 5: AST Evaluation and Report OOM Error State Propagation

**Files:**
- Modify: `src/vm/eval.c:710-1010`
- Test: `tests/test_execution.c`

- [x] **Step 1: Write the failing test for Report OOM error propagation**

In `tests/test_execution.c`:
```c
static void
test_execution_report_oom(void)
{
	TEST_BEGIN("execution report OOM sets CDSL_ERR_RESOURCE_LIMIT");

	cdsl_schema_t* s = cdsl_schema_create();
	cdsl_schema_register_var(s, "age", CDSL_TYPE_INT);
	cdsl_vm_t* vm = cdsl_vm_create(s);
	cdsl_context_t* ctx = cdsl_context_create(s);
	cdsl_context_set_int(ctx, "age", 25);

	cdsl_rule_t* rule = cdsl_parse_string(
	    "RULE r { META { description = \"test\" } WHEN age > 18 THEN block() }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule parsed");

	/* Force OOM during report construction */
	cdsl_test_set_oom_countdown(0);
	cdsl_rule_report_t* rpt = cdsl_vm_execute(vm, rule, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "Report returned for error state");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_ERROR, "Status is ERROR");
	TEST_ASSERT_NOT_NULL(rpt->error, "Report carries error object");
	if (rpt->error) {
		TEST_ASSERT_INT(rpt->error->kind, CDSL_ERR_RESOURCE_LIMIT, "Error kind is RESOURCE_LIMIT");
	}

	cdsl_report_free(rpt);
	cdsl_test_set_oom_countdown(-1);
	cdsl_free_rule(rule);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(s);
	TEST_END();
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_execution && ./build/test_execution`
Expected: Segmentation fault or NULL dereference because `report->rule_name = strdup(...)` is not checked.

- [x] **Step 3: Implement clean OOM checking in `src/vm/eval.c`**

In `execute_simple_rule` and `execute_metric_rule`:
1. Check `report->rule_name` and `report->description`. If NULL, call:
   ```c
   cdsl_vm_set_last_error(vm, CDSL_ERR_RESOURCE_LIMIT, 0, 0,
                          "Out of memory allocating rule report", NULL);
   CDSL_ATOMIC_STORE(&vm->error_state, 1);
   cdsl_report_free(report);
   return NULL;
   ```
2. In `metrics[0]` / `mr`: check `metric_name` and `description`. If NULL, goto error cleanup.
3. In `violation_reason`: check `cdsl_strdup`. If NULL, goto error cleanup.
4. In `decision_summary`: check `cdsl_strdup`. If NULL, goto error cleanup.
5. In `cdsl_vm_execute`: if `CDSL_ATOMIC_LOAD(&vm->error_state)` is set and `rpt == NULL`, allocate an emergency `calloc(1, sizeof(cdsl_rule_report_t))`, populate `rpt->status = CDSL_STATUS_ERROR` and transfer `vm->last_error` to `rpt->error`.

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_execution && ./build/test_execution`
Expected: PASS with 0 failures.

- [x] **Step 5: Commit**

```bash
git add src/vm/eval.c tests/test_execution.c
git commit -m "fix(eval): 🐛 propagate CDSL_ERR_RESOURCE_LIMIT on strdup failure during rule evaluation"
```

---

### Task 6: Bytecode Compilation & Execution OOM Rollback

**Files:**
- Modify: `src/vm/bytecode.c:55-80, 800-845, 1500-1695`
- Test: `tests/test_bytecode.c`

- [x] **Step 1: Write the failing test for Bytecode OOM safety**

In `tests/test_bytecode.c`:
```c
static void
test_bytecode_compile_and_execute_oom(void)
{
	TEST_BEGIN("bytecode compile and execute OOM safety");

	cdsl_schema_t* s = cdsl_schema_create();
	cdsl_schema_register_var(s, "age", CDSL_TYPE_INT);
	cdsl_vm_t* vm = cdsl_vm_create(s);
	cdsl_context_t* ctx = cdsl_context_create(s);
	cdsl_context_set_int(ctx, "age", 25);

	cdsl_rule_t* rule = cdsl_parse_string(
	    "RULE r { META { description = \"test\" } WHEN age > 18 THEN block() }", NULL);
	TEST_ASSERT_NOT_NULL(rule, "Rule parsed");

	/* 1. Compile under OOM */
	cdsl_bytecode_t bc = {0};
	cdsl_test_set_oom_countdown(0);
	int ok = cdsl_bytecode_compile(rule, s, &bc);
	TEST_ASSERT_INT(ok, 0, "bytecode compile under OOM returns 0");
	cdsl_bytecode_free(&bc); /* must not crash or leak */

	/* 2. Execute under OOM */
	cdsl_test_set_oom_countdown(-1);
	ok = cdsl_bytecode_compile(rule, s, &bc);
	TEST_ASSERT_INT(ok, 1, "bytecode compile succeeds");

	cdsl_test_set_oom_countdown(0);
	cdsl_rule_report_t* rpt = cdsl_bytecode_execute_rule(vm, &bc, ctx);
	TEST_ASSERT_NOT_NULL(rpt, "Report returned for error state");
	TEST_ASSERT_INT(rpt->status, CDSL_STATUS_ERROR, "Status is ERROR on OOM");
	cdsl_report_free(rpt);

	cdsl_test_set_oom_countdown(-1);
	cdsl_bytecode_free(&bc);
	cdsl_free_rule(rule);
	cdsl_context_free(ctx);
	cdsl_vm_free(vm);
	cdsl_schema_free(s);
	TEST_END();
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_bytecode && ./build/test_bytecode`
Expected: FAIL because compile does not check string constant duplication or report allocation in `cdsl_bytecode_execute_rule`.

- [x] **Step 3: Implement clean OOM checking in `src/vm/bytecode.c`**

1. In `bc_add_constant`:
   - `char* str_dup = cdsl_strdup(val.data.string_val);`
   - If `!str_dup`, return `-1`.
2. In `cdsl_bytecode_compile`:
   - Duplicate `bc->rule_name`, `bc->description`, and `bc->metric_infos[i].name / description`.
   - If any `cdsl_strdup` fails or `bc_add_constant` returns `-1`: call `cdsl_bytecode_free(bc)` and return `0`.
3. In `cdsl_bytecode_execute_rule`:
   - Check `report->rule_name` and `report->description`. If NULL, set `vm->error_state = 1`, record `CDSL_ERR_RESOURCE_LIMIT`, set `report->status = CDSL_STATUS_ERROR`.
   - In `bc_rule_metric_handler`: check `mr->metric_name`, `mr->description`, `mr->violation_reason`. On failure, return `0` (abort execution).
   - In `bc_finalize_simple_rule_report`: check `m->metric_name`, `m->description`, `report->decision_summary`. If NULL, mark `report->status = CDSL_STATUS_ERROR`.

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_bytecode && ./build/test_bytecode`
Expected: PASS with 0 failures.

- [x] **Step 5: Commit**

```bash
git add src/vm/bytecode.c tests/test_bytecode.c
git commit -m "fix(bytecode): 🐛 handle strdup failure in bytecode compile and execution"
```

---

### Task 7: Error Node Fallback & Ruleset String Allocations

**Files:**
- Modify: `src/util/error.c:45-75`
- Modify: `src/vm/ruleset.c:170-185, 430-445, 590-695`
- Test: `tests/test_error.c`, `tests/test_ruleset.c`

- [x] **Step 1: Write the failing test for `cdsl_error_create` OOM fallback**

In `tests/test_error.c`:
```c
static void
test_error_create_oom_fallback(void)
{
	TEST_BEGIN("error_create fallback on strdup OOM");

	cdsl_test_set_oom_countdown(0);
	cdsl_error_t* err = cdsl_error_create(CDSL_ERR_RUNTIME, 10, 5, "My error message", "My hint");
	/* Even if allocation failed, error creation must either safely return an emergency object or clean NULL without crashing */
	if (err) {
		TEST_ASSERT_NOT_NULL(err->message, "Emergency message is present");
		cdsl_error_free(err);
	}

	cdsl_test_set_oom_countdown(-1);
	TEST_END();
}
```

- [x] **Step 2: Run test to verify it fails**

Run: `cmake --build build --target test_error && ./build/test_error`
Expected: Crash or failure when `strdup` returns NULL.

- [x] **Step 3: Implement fallback in `src/util/error.c` and `src/vm/ruleset.c`**

In `src/util/error.c`:
- In `cdsl_error_create`:
  ```c
  if (message) {
      e->message = cdsl_strdup(message);
      if (!e->message) {
          e->message = cdsl_strdup("Out of memory");
          if (!e->message) {
              free(e);
              return NULL;
          }
      }
  }
  if (hint) {
      e->hint = cdsl_strdup(hint); /* hint is optional */
  }
  ```
In `src/vm/ruleset.c`:
- Check `batch->summary = cdsl_strdup(buf);`. If NULL, ensure `batch->summary` remains NULL without crashing `cdsl_batch_report_free()`.
- In dependency resolution (`char* dep_buf = cdsl_strdup(deps)`): if NULL, handle OOM gracefully and return failure.

- [x] **Step 4: Run test to verify it passes**

Run: `cmake --build build --target test_error && cmake --build build --target test_ruleset && ctest --test-dir build -R "test_error|test_ruleset"`
Expected: PASS.

- [x] **Step 5: Commit**

```bash
git add src/util/error.c src/vm/ruleset.c tests/test_error.c
git commit -m "fix(error): 🐛 provide safe emergency fallback for error construction under OOM"
```

---

### Task 8: Comprehensive OOM Fault Injection Tests & Sanitizer Verification

**Files:**
- Modify: `tests/test_memory.c`
- Test: All 28 C tests + 104 Python tests

- [x] **Step 1: Write comprehensive fault injection suite in `tests/test_memory.c`**

Add tests exercising:
1. `test_context_deep_oom()`: iterating countdown from 0 to 5 over JSON context loading and nested key setting.
2. `test_schema_deep_oom()`: iterating countdown over registration of 10 variables and actions.
3. `test_eval_deep_oom()`: iterating countdown over multi-metric rule execution, asserting `report->status == CDSL_STATUS_ERROR` and 0 memory leaks.
4. `test_bytecode_deep_oom()`: iterating countdown over compilation and execution.

- [x] **Step 2: Run all tests with CMake and CTest**

Run:
```bash
cmake --build build --target format && cmake --build build --target check-format
ctest --test-dir build --output-on-failure
pytest python/tests
```
Expected: 100% PASS across all 28 C tests and 104 pytest tests.

- [x] **Step 3: Run full suite with AddressSanitizer and UndefinedBehaviorSanitizer**

Run:
```bash
cmake -B build-asan -DCMAKE_C_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan -j$(nproc)
ctest --test-dir build-asan --output-on-failure
rm -rf build-asan
```
Expected: All tests pass with zero leaks, zero memory errors, zero undefined behavior.

- [x] **Step 4: Commit**

```bash
git add tests/test_memory.c
git commit -m "test(memory): ✅ add comprehensive OOM fault injection test suite"
```
