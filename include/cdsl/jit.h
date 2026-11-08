/**
 * @file include/cdsl/jit.h
 * @brief Machine code JIT compiler for CDSL bytecode.
 *
 * Compiles CDSL linear bytecode into native machine code (x86-64 and AArch64)
 * with W^X executable memory protection and automatic fallback to threaded
 * execution.
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
 * @brief Compile a bytecode chunk into native machine code or JIT trampoline.
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

#ifdef __cplusplus
}
#endif

#endif /* CDSL_JIT_H */
