/**
 * @file portability.h
 * @brief Portability macros for different compilers and standards.
 */

#ifndef CDSL_PORTABILITY_H
#define CDSL_PORTABILITY_H

/* [[nodiscard]] / warn_unused_result */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 202311L
#define CDSL_NODISCARD [[nodiscard]]
#elif defined(__cplusplus) && __cplusplus >= 201703L
#define CDSL_NODISCARD [[nodiscard]]
#elif defined(__GNUC__) || defined(__clang__)
#define CDSL_NODISCARD __attribute__((warn_unused_result))
#else
#define CDSL_NODISCARD
#endif

/* static_assert */
#include <assert.h>

/* MSVC-specific fixes */
#ifdef _MSC_VER
/* Disable sscanf safety warnings */
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

/* strdup is deprecated in MSVC, use _strdup */
#include <string.h>
#define strdup _strdup

/* MSVC doesn't support C11 stdatomic.h well without special flags */
#if !defined(__cplusplus) && (!defined(_MSVC_LANG) || _MSVC_LANG < 202002L)
#define CDSL_NO_STDATOMIC
#endif
#endif

/* Atomics wrapper */
#ifdef CDSL_NO_STDATOMIC
#include <stdint.h>
typedef long cdsl_atomic_long_t;
typedef int cdsl_atomic_int_t;
typedef int64_t cdsl_atomic_int64_t;
#define CDSL_ATOMIC_LOAD(p) (*(p))
#define CDSL_ATOMIC_STORE(p, v) (*(p) = (v))
#define CDSL_ATOMIC_FETCH_ADD(p, v) (*(p) += (v), *(p) - (v))
#define CDSL_ATOMIC_FETCH_SUB(p, v) (*(p) -= (v), *(p) + (v))
#else
#include <stdatomic.h>
#include <stdint.h>
typedef _Atomic long cdsl_atomic_long_t;
typedef _Atomic int cdsl_atomic_int_t;
typedef _Atomic int64_t cdsl_atomic_int64_t;
#define CDSL_ATOMIC_LOAD(p) atomic_load(p)
#define CDSL_ATOMIC_STORE(p, v) atomic_store(p, v)
#define CDSL_ATOMIC_FETCH_ADD(p, v) atomic_fetch_add(p, v)
#define CDSL_ATOMIC_FETCH_SUB(p, v) atomic_fetch_sub(p, v)
#endif

#endif /* CDSL_PORTABILITY_H */
