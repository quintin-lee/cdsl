/**
 * @file src/vm/jit.c
 * @brief Architecture-specific native machine-code JIT trampoline compiler and dispatch layer.
 *
 * Implements native trampoline code generation for x86-64 and AArch64 with W^X executable
 * memory management and per-compiled-rule caching to bridge native function calls to VM execution.
 *
 * Architecture & Relocation Notes:
 * 1. 64-Bit Immediate Addressing:
 *    The trampoline compiler explicitly generates 64-bit absolute immediate pointers
 *    (`movabs rax, imm64` / literal pool `ldr x16, [pc, #offset]`) to reference
 *    `cdsl_jit_native_helper` and `cdsl_bytecode_t`. This deliberate design eliminates
 *    32-bit PC-relative displacement limitations (which constrain PLT/GOT stubs to ±2GB)
 *    and enables JIT pages to be mapped arbitrarily in the 64-bit virtual address space.
 * 2. Shared-Library & Dynamic Linking Lifecycle:
 *    Because the trampoline embeds absolute function pointers resolved at runtime:
 *    - Trampolines are strictly bound to the lifetime of the loaded shared library image
 *      (`libcdsl.so` or `cdsl.dll`). If the library is unloaded via `dlclose()` or `FreeLibrary()`,
 *      all existing trampolines become dangling pointers. Callers must destroy all `cdsl_jit_code_t`
 *      handles before library teardown.
 *    - Trampolines cannot be persisted or shared between processes due to ASLR.
 * 3. Security (W^X):
 *    Pages are mapped as RW, populated with instructions, transitioned to RX via
 *    mprotect/VirtualProtect, and synchronized via architecture instruction cache flushing.
 * 4. AArch64 Layout-Computed Pool Offsets:
 *    The two LDR (literal) instructions in the AArch64 trampoline use imm9 values
 *    that are back-filled from the actual prologue layout (imm9 = n - idx, where n
 *    is the prologue instruction count and idx is the LDR's index). This removes
 *    the historical risk of hand-filled offsets (#20/#24) silently desynchronizing
 *    when prologue instructions are added or removed.
 */

#include "cdsl/jit.h"
#include "cdsl/util/threads.h"
#include "internal.h"
#include "value_ops.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#ifndef MAP_ANONYMOUS
#ifdef MAP_ANON
#define MAP_ANONYMOUS MAP_ANON
#else
#define MAP_ANONYMOUS 0x1000
#endif
#endif
#endif

/* Check CPU architecture */
#if defined(__x86_64__) || defined(_M_X64)
#define CDSL_JIT_ARCH_X86_64 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#define CDSL_JIT_ARCH_AARCH64 1
#else
#define CDSL_JIT_ARCH_GENERIC 1
#endif

bool
cdsl_jit_is_supported(void)
{
#if defined(CDSL_JIT_ARCH_X86_64) || defined(CDSL_JIT_ARCH_AARCH64)
	return true;
#else
	return false;
#endif
}

static void*
jit_alloc_pages(size_t size, size_t* out_actual_size)
{
#ifdef _WIN32
	SYSTEM_INFO si;
	GetSystemInfo(&si);
	size_t page_size = si.dwPageSize > 0 ? (size_t)si.dwPageSize : 4096;
	size_t alloc_size = (size + page_size - 1) & ~(page_size - 1);
	void* p = VirtualAlloc(NULL, alloc_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	if (out_actual_size) {
		*out_actual_size = alloc_size;
	}
	return p;
#else
	long ps = sysconf(_SC_PAGESIZE);
	size_t page_size = ps > 0 ? (size_t)ps : 4096;
	size_t alloc_size = (size + page_size - 1) & ~(page_size - 1);
	void* p =
	    mmap(NULL, alloc_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (p == MAP_FAILED) {
		p = NULL;
	}
	if (out_actual_size) {
		*out_actual_size = alloc_size;
	}
	return p;
#endif
}

static int
jit_protect_executable(void* p, size_t size)
{
	if (!p) {
		return -1;
	}
#ifdef _WIN32
	DWORD old_prot = 0;
	if (!VirtualProtect(p, size, PAGE_EXECUTE_READ, &old_prot)) {
		return -1;
	}
	FlushInstructionCache(GetCurrentProcess(), p, size);
	return 0;
#else
	if (mprotect(p, size, PROT_READ | PROT_EXEC) != 0) {
		return -1;
	}
#if defined(__GNUC__) || defined(__clang__)
	__builtin___clear_cache((char*)p, (char*)p + size);
#endif
	return 0;
#endif
}

static void
jit_free_pages(void* p, size_t size)
{
	if (!p) {
		return;
	}
#ifdef _WIN32
	(void)size;
	VirtualFree(p, 0, MEM_RELEASE);
#else
	munmap(p, size);
#endif
}

/**
 * @brief Runtime helper called by native JIT machine code.
 */
static int
cdsl_jit_native_helper(cdsl_vm_t* vm,
		       const cdsl_bytecode_t* bc,
		       cdsl_context_t* ctx,
		       cdsl_value_t* out_result)
{
	if (!bc || !out_result) {
		return -1;
	}
	int rc = cdsl_bc_run_threaded(vm, bc, ctx, NULL, NULL, out_result);
	return rc ? 0 : -1;
}

static int
jit_fallback_trampoline(cdsl_vm_t* vm, cdsl_context_t* ctx, cdsl_value_t* out_result)
{
	if (!out_result) {
		return -1;
	}
	/* Return void value if no bytecode bound */
	out_result->type = CDSL_TYPE_VOID;
	out_result->data.int_val = 0;
	(void)vm;
	(void)ctx;
	return 0;
}

cdsl_jit_code_t*
cdsl_jit_compile(cdsl_vm_t* vm, const cdsl_bytecode_t* bc)
{
	if (!bc) {
		return NULL;
	}

	cdsl_jit_code_t* jit = calloc(1, sizeof(cdsl_jit_code_t));
	if (!jit) {
		return NULL;
	}
	jit->bc = bc;
	jit->is_native = false;
	jit->entry_point = jit_fallback_trampoline;

#if defined(CDSL_JIT_ARCH_X86_64) || defined(CDSL_JIT_ARCH_AARCH64)
	size_t actual_size = 0;
	void* page = jit_alloc_pages(4096, &actual_size);
	if (!page) {
		/* Fallback to non-native */
		return jit;
	}

	uint8_t* code = (uint8_t*)page;
	size_t ip = 0;

#if defined(CDSL_JIT_ARCH_X86_64) && !defined(_WIN32)
	/*
	 * System V AMD64 ABI:
	 * int jit_entry(rdi = vm, rsi = ctx, rdx = out_result)
	 *
	 * Prologue:
	 *   push rbp
	 *   mov rbp, rsp
	 *   sub rsp, 32
	 *   mov [rbp-8], rdi     ; vm
	 *   mov [rbp-16], rsi    ; ctx
	 *   mov [rbp-24], rdx    ; out_result
	 */
	code[ip++] = 0x55; /* push rbp */
	code[ip++] = 0x48;
	code[ip++] = 0x89;
	code[ip++] = 0xe5; /* mov rbp, rsp */
	code[ip++] = 0x48;
	code[ip++] = 0x83;
	code[ip++] = 0xec;
	code[ip++] = 0x20; /* sub rsp, 32 */
	code[ip++] = 0x48;
	code[ip++] = 0x89;
	code[ip++] = 0x7d;
	code[ip++] = 0xf8; /* mov [rbp-8], rdi */
	code[ip++] = 0x48;
	code[ip++] = 0x89;
	code[ip++] = 0x75;
	code[ip++] = 0xf0; /* mov [rbp-16], rsi */
	code[ip++] = 0x48;
	code[ip++] = 0x89;
	code[ip++] = 0x55;
	code[ip++] = 0xe8; /* mov [rbp-24], rdx */

	/*
	 * Call cdsl_jit_native_helper(rdi = vm, rsi = bc, rdx = ctx, rcx = out_result)
	 *
	 * mov rdi, [rbp-8]       ; vm
	 * mov rsi, imm64(bc)     ; bc
	 * mov rdx, [rbp-16]      ; ctx
	 * mov rcx, [rbp-24]      ; out_result
	 * mov rax, imm64(helper) ; helper fn
	 * call rax
	 */
	code[ip++] = 0x48;
	code[ip++] = 0x8b;
	code[ip++] = 0x7d;
	code[ip++] = 0xf8; /* mov rdi, [rbp-8] */

	/* mov rsi, imm64 */
	code[ip++] = 0x48;
	code[ip++] = 0xbe;
	uint64_t bc_addr = (uint64_t)(uintptr_t)bc;
	memcpy(code + ip, &bc_addr, sizeof(bc_addr));
	ip += sizeof(bc_addr);

	code[ip++] = 0x48;
	code[ip++] = 0x8b;
	code[ip++] = 0x55;
	code[ip++] = 0xf0; /* mov rdx, [rbp-16] */
	code[ip++] = 0x48;
	code[ip++] = 0x8b;
	code[ip++] = 0x4d;
	code[ip++] = 0xe8; /* mov rcx, [rbp-24] */

	/* mov rax, imm64 */
	code[ip++] = 0x48;
	code[ip++] = 0xb8;
	uint64_t helper_addr = (uint64_t)(uintptr_t)cdsl_jit_native_helper;
	memcpy(code + ip, &helper_addr, sizeof(helper_addr));
	ip += sizeof(helper_addr);

	code[ip++] = 0xff;
	code[ip++] = 0xd0; /* call rax */

	/*
	 * Epilogue:
	 *   add rsp, 32
	 *   pop rbp
	 *   ret
	 */
	code[ip++] = 0x48;
	code[ip++] = 0x83;
	code[ip++] = 0xc4;
	code[ip++] = 0x20; /* add rsp, 32 */
	code[ip++] = 0x5d; /* pop rbp */
	code[ip++] = 0xc3; /* ret */

#elif defined(CDSL_JIT_ARCH_X86_64) && defined(_WIN32)
	/*
	 * Windows x64 ABI:
	 * int jit_entry(rcx = vm, rdx = ctx, r8 = out_result)
	 *
	 * Prologue:
	 *   sub rsp, 40          ; 32 shadow space + 8 alignment
	 */
	code[ip++] = 0x48;
	code[ip++] = 0x83;
	code[ip++] = 0xec;
	code[ip++] = 0x28;

	/*
	 * Call cdsl_jit_native_helper(rcx = vm, rdx = bc, r8 = ctx, r9 = out_result)
	 *
	 * mov r9, r8             ; out_result
	 * mov r8, rdx            ; ctx
	 * mov rdx, imm64(bc)     ; bc
	 * ; rcx is already vm
	 * mov rax, imm64(helper)
	 * call rax
	 */
	code[ip++] = 0x4d;
	code[ip++] = 0x89;
	code[ip++] = 0xc1; /* mov r9, r8 */
	code[ip++] = 0x49;
	code[ip++] = 0x89;
	code[ip++] = 0xd0; /* mov r8, rdx */

	/* mov rdx, imm64 */
	code[ip++] = 0x48;
	code[ip++] = 0xba;
	uint64_t bc_addr = (uint64_t)(uintptr_t)bc;
	memcpy(code + ip, &bc_addr, sizeof(bc_addr));
	ip += sizeof(bc_addr);

	/* mov rax, imm64 */
	code[ip++] = 0x48;
	code[ip++] = 0xb8;
	uint64_t helper_addr = (uint64_t)(uintptr_t)cdsl_jit_native_helper;
	memcpy(code + ip, &helper_addr, sizeof(helper_addr));
	ip += sizeof(helper_addr);

	code[ip++] = 0xff;
	code[ip++] = 0xd0; /* call rax */

	/*
	 * Epilogue:
	 *   add rsp, 40
	 *   ret
	 */
	code[ip++] = 0x48;
	code[ip++] = 0x83;
	code[ip++] = 0xc4;
	code[ip++] = 0x28; /* add rsp, 40 */
	code[ip++] = 0xc3; /* ret */

#elif defined(CDSL_JIT_ARCH_AARCH64)
	/*
	 * AArch64 AAPCS:
	 * int jit_entry(x0 = vm, x1 = ctx, x2 = out_result)
	 *
	 * Prologue:
	 *   stp x29, x30, [sp, #-32]!
	 *   mov x29, sp
	 *   stp x0, x1, [sp, #16]
	 */
	uint32_t instrs[32];
	size_t n = 0;

	instrs[n++] = 0xa9be7bfd; /* stp x29, x30, [sp, #-32]! */
	instrs[n++] = 0x910003fd; /* mov x29, sp */

	/*
	 * Arrange args for helper(x0=vm, x1=bc, x2=ctx, x3=out_result)
	 *   mov x3, x2      ; out_result
	 *   mov x2, x1      ; ctx
	 *   ldr x1, [pc, #imm9*4]  ; bc_val (imm9 back-filled below)
	 *   ldr x16, [pc, #imm9*4]  ; helper_val (imm9 back-filled below)
	 *   blr x16
	 */
	instrs[n++] = 0xaa0203e3; /* mov x3, x2 */
	instrs[n++] = 0xaa0103e2; /* mov x2, x1 */

	/* LDR (literal) placeholders; imm9 is back-filled after the prologue is
	 * final, computed from the actual layout so adding/removing prologue
	 * instructions cannot silently desynchronize the offsets */
	size_t idx_bc = n;
	instrs[n++] = 0x58000000u; /* ldr x1, [pc, #imm9*4] */
	size_t idx_helper = n;
	instrs[n++] = 0x58000000u; /* ldr x16, [pc, #imm9*4] */
	instrs[n++] = 0xd63f0200;  /* blr x16 */

	/* Epilogue */
	instrs[n++] = 0xa8c27bfd; /* ldp x29, x30, [sp], #32 */
	instrs[n++] = 0xd65f03c0; /* ret */

	/*
	 * The literal pool sits at byte offset 4*n (immediately after the
	 * prologue). AArch64 LDR (literal) resolves PC as the address of the
	 * LDR instruction itself, so imm9 = (pool - ldr_pc)/4 = n - idx.
	 */
	uint32_t imm9_bc = (uint32_t)(n - idx_bc);
	uint32_t imm9_helper = (uint32_t)(n - idx_helper);
	if (imm9_bc > 4095u || imm9_helper > 4095u) {
		jit_free_pages(page, actual_size);
		free(jit);
		return NULL;
	}
	instrs[idx_bc] = 0x58000000u | (imm9_bc << 10) | 0x01u;		/* rt = x1 */
	instrs[idx_helper] = 0x58000000u | (imm9_helper << 10) | 0x10u; /* rt = x16 */

	/* Literal pool */
	memcpy(code, instrs, n * sizeof(uint32_t));
	ip = n * sizeof(uint32_t);

	uint64_t bc_val = (uint64_t)(uintptr_t)bc;
	memcpy(code + ip, &bc_val, sizeof(bc_val));
	ip += sizeof(bc_val);

	uint64_t helper_val = (uint64_t)(uintptr_t)cdsl_jit_native_helper;
	memcpy(code + ip, &helper_val, sizeof(helper_val));
	ip += sizeof(helper_val);
#endif

	if (jit_protect_executable(page, actual_size) == 0) {
		jit->exec_mem = page;
		jit->mem_size = actual_size;
		memcpy(&jit->entry_point, &page, sizeof(page));
		jit->is_native = true;
	} else {
		jit_free_pages(page, actual_size);
	}
#endif

	(void)vm;
	return jit;
}

int
cdsl_jit_execute(cdsl_jit_code_t* code,
		 cdsl_vm_t* vm,
		 cdsl_context_t* ctx,
		 cdsl_value_t* out_result)
{
	if (!code || !out_result) {
		return -1;
	}
	code->exec_count++;
	if (code->is_native && code->entry_point) {
		return code->entry_point(vm, ctx, out_result);
	}
	if (code->bc) {
		return cdsl_jit_native_helper(vm, code->bc, ctx, out_result);
	}
	return -1;
}

void
cdsl_jit_code_free(cdsl_jit_code_t* code)
{
	if (!code) {
		return;
	}
	if (code->exec_mem && code->mem_size > 0) {
		jit_free_pages(code->exec_mem, code->mem_size);
	}
	free(code);
}
