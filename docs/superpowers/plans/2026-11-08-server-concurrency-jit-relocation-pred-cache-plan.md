# Server Concurrency, JIT Relocation Notes, and Predicate Cache Invalidation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement true `max_concurrency` worker thread pool and dynamic response buffering in `cdsl_server`, document JIT shared-library relocation constraints, and formalize predicate cache invalidation semantics with automatic context binding in sequential rulesets.

---

## File Structure

- **Modified Files**:
  - `include/cdsl/util/threads.h`: Add `cdsl_cond_t` and `CDSL_COND_*` macros for cross-platform condition variable support.
  - `src/server/server.c`: Refactor to worker thread pool, dynamic HTTP response generation, and complete socket transmission loop.
  - `include/cdsl/server.h`: Clarify thread pool configuration in docstrings.
  - `include/cdsl/cse.h`: Formalize invalidation semantics, thread safety, and parallel bypass behavior for `cdsl_pred_cache_bind_context`.
  - `src/vm/ruleset.c`: Auto-bind `ctx` in `cdsl_vm_execute_ruleset`, document isolation in `cdsl_vm_execute_ruleset_parallel`.
  - `include/cdsl/jit.h`: Document 64-bit pointer embedding, shared-library relocation constraints, and lifecycle bounds.
  - `src/vm/jit.c`: Add in-depth architecture comments explaining 64-bit absolute immediate addressing and dlclose hazards.
  - `README.md` & `CHANGELOG.md`: Record v2.0.2 release updates.
- **Test Files**:
  - `tests/test_server.c`: Add multi-client concurrency tests and large dynamic payload response test.
  - `tests/test_cse.c`: Add test verifying context mutation invalidation during sequential ruleset execution.

---

### Task 1: Cross-Platform Condition Variables in `threads.h`
- [x] Add `cdsl_cond_t`, `CDSL_COND_INIT`, `CDSL_COND_WAIT`, `CDSL_COND_SIGNAL`, `CDSL_COND_BROADCAST`, and `CDSL_COND_DESTROY` for POSIX and Windows.

### Task 2: Server Dynamic Buffering & `max_concurrency` Worker Thread Pool
- [x] Refactor response generation to dynamic buffers (`server_http_response`, dynamic metrics and evaluation JSON strings).
- [x] Implement socket queue and listener thread (`server_listener_routine`) + worker thread pool (`server_worker_routine`) sized by `config.max_concurrency`.
- [x] Ensure graceful shutdown: signal workers, drain queue, join all threads.
- [x] Add concurrency and large dynamic response tests in `tests/test_server.c`.

### Task 3: Predicate Cache Invalidation & Sequential Ruleset Auto-Binding
- [x] Update `include/cdsl/cse.h` with invalidation and parallel isolation documentation.
- [x] Update `src/vm/ruleset.c` to automatically bind `ctx` to `vm->pred_cache` in `cdsl_vm_execute_ruleset` and unbind at end.
- [x] Add test case in `tests/test_cse.c` verifying automatic invalidation upon context modification.

### Task 4: JIT Relocation & Shared-Library Documentation
- [x] Document 64-bit pointer embedding and dynamic relocation constraints in `include/cdsl/jit.h` and `src/vm/jit.c`.
- [x] Update `README.md` and `CHANGELOG.md` for v2.0.2.

### Task 5: Local Regression & Full GitHub Actions CI Verification
- [x] Run full local test suite `ctest --test-dir build --output-on-failure`.
- [x] Verify `clang-format --dry-run --Werror`.
- [x] Push to `origin master` and monitor CI run to ensure all 13 jobs pass.
