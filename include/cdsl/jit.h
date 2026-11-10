/**
 * @file include/cdsl/jit.h
 * @brief Native machine code JIT trampoline compiler and dispatch layer for CDSL bytecode.
 *
 * Implements an architecture-specific native trampoline layer for x86-64 (System V AMD64
 * and Windows x64) and AArch64 (Linux and macOS Apple Silicon ARM64) with portable W^X
 * executable memory protection.
 *
 * Trampoline stubs translate native function calls directly into the VM execution pipeline,
 * and are cached per compiled bytecode rule (`bc->jit_code`) to eliminate repeated compilation
 * overhead while offering seamless native C function pointer interop.
 *
 * @section jit_relocation Relocation & Shared-Library Constraints
 * - 64-Bit Absolute Pointer Embedding: Trampolines embed absolute 64-bit immediate pointers
 *   to `cdsl_jit_native_helper` and `cdsl_bytecode_t` data structures directly into machine code
 *   (via `movabs rax, imm64` on x86-64 and literal pool `ldr x16, [pc, #offset]` on AArch64).
 *   This avoids 32-bit PC-relative displacement overflow (such as ±2GB limits of PLT/GOT calls)
 *   when executable pages are allocated in distant address spaces.
 * - Dynamic Library Lifecycle Bounds: The embedded absolute addresses are valid only for the
 *   lifetime of the loaded library image. If `libcdsl.so` or `cdsl.dll` is unloaded via
 *   `dlclose()` or `FreeLibrary()`, any existing JIT trampolines become dangling pointers.
 *   Callers must release all `cdsl_jit_code_t` handles before unloading the shared library.
 *   `cdsl_jit_teardown()` makes this contract enforceable: it frees every registered
 *   trampoline and reports how many were still live, so integrators can assert a clean
 *   state before tearing down the library.
 * - Non-Serializable: Due to Address Space Layout Randomization (ASLR), compiled trampolines
 *   cannot be serialized to persistent storage or shared across process address spaces.
 * - W^X & Security Enforcement: On platforms with strict code signing or W^X memory policies,
 *   if executable page allocation or permission transition fails, `cdsl_jit_compile()`
 *   transparently degrades to an interpretive fallback with `is_native = false`.
 */

#ifndef CDSL_JIT_H
#define CDSL_JIT_H

#include "cdsl/bytecode.h"
#include "cdsl/context.h"
#include "cdsl/vm.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cdsl_jit_code cdsl_jit_code_t;

/**
 * @brief Function pointer signature for JIT-compiled rule/expression code.
 * @param vm VM instance
 * @param ctx Execution context
 * @param out_result Output value
 * @return 0 on success, non-zero on error
 */
typedef int (*cdsl_jit_fn)(cdsl_vm_t* vm, cdsl_context_t* ctx, cdsl_value_t* out_result);

/**
 * @brief Compiled JIT object.
 */
struct cdsl_jit_code {
	void* exec_mem;
	size_t mem_size;
	cdsl_jit_fn entry_point;
	const cdsl_bytecode_t* bc;
	bool is_native;
	uint64_t exec_count;
};

/**
 * @brief Check whether native JIT compilation is supported on the current CPU architecture.
 * @return true if native machine code generation is available
 */
bool cdsl_jit_is_supported(void);

/**
 * @brief Compile a bytecode chunk into a native machine code trampoline for direct fast-path execution.
 * @param vm Virtual machine instance
 * @param bc Bytecode chunk to compile
 * @return Compiled JIT code object, or NULL on allocation error
 */
cdsl_jit_code_t* cdsl_jit_compile(cdsl_vm_t* vm, const cdsl_bytecode_t* bc);

/**
 * @brief Execute a compiled JIT code object against a context.
 * @param code Compiled JIT code
 * @param vm Virtual machine instance
 * @param ctx Execution context
 * @param out_result Pointer to store evaluated result
 * @return 0 on success, non-zero on runtime error
 */
int cdsl_jit_execute(cdsl_jit_code_t* code,
		     cdsl_vm_t* vm,
		     cdsl_context_t* ctx,
		     cdsl_value_t* out_result);

/**
 * @brief Free compiled JIT code object and release executable memory.
 * @param code Compiled JIT code object
 */
void cdsl_jit_code_free(cdsl_jit_code_t* code);

/**
 * @brief Release every registered JIT trampoline, returning how many were still live.
 *
 * The JIT layer tracks every code object produced by `cdsl_jit_compile()` in an
 * internal registry, so the shared library can detect and reclaim trampolines
 * that a caller forgot to free.  Call this before unloading `libcdsl.so` /
 * `cdsl.dll` (via `dlclose` / `FreeLibrary`) so no dangling native entry
 * points survive.  Frees all registered code objects, clears the registry, and
 * returns the number of live trampolines that were reclaimed (0 if the caller
 * already freed them all).
 *
 * @return Number of live trampolines freed by this call.
 */
size_t cdsl_jit_teardown(void);

#ifdef __cplusplus
}
#endif

#endif /* CDSL_JIT_H */
