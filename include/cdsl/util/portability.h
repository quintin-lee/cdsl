/**
 * @file portability.h
 * @brief Portability macros for different compilers and standards.
 */

#ifndef CDSL_PORTABILITY_H
#define CDSL_PORTABILITY_H

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

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

/* printf format attribute */
#if defined(__GNUC__) || defined(__clang__)
#define CDSL_PRINTF_FORMAT(a, b) __attribute__((format(printf, a, b)))
#else
#define CDSL_PRINTF_FORMAT(a, b)
#endif

/* Thread-local storage specifier */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L && !defined(__STDC_NO_THREADS__)
#define THREAD_LOCAL _Thread_local
#elif defined(__GNUC__) || defined(__clang__)
#define THREAD_LOCAL __thread
#elif defined(_MSC_VER)
#define THREAD_LOCAL __declspec(thread)
#else
#define THREAD_LOCAL
#endif

/* MSVC-specific fixes */
#ifdef _MSC_VER
#ifndef _CRT_SECURE_NO_WARNINGS
#define _CRT_SECURE_NO_WARNINGS
#endif

#define strdup _strdup
#define popen _popen
#define pclose _pclose

static inline struct tm*
cdsl_localtime_r(const time_t* timer, struct tm* buf)
{
	if (localtime_s(buf, timer) == 0) {
		return buf;
	}
	return NULL;
}
#define CDSL_LOCALTIME_R cdsl_localtime_r

/* MSVC doesn't have clock_gettime, use QueryPerformanceCounter wrapper or similar */
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

#if !defined(__cplusplus) && (!defined(_MSVC_LANG) || _MSVC_LANG < 202002L)
#define CDSL_NO_STDATOMIC
#endif
#else
#define CDSL_LOCALTIME_R localtime_r
#endif

/* Atomics wrapper */
#ifdef CDSL_NO_STDATOMIC
typedef long cdsl_atomic_long_t;
typedef int cdsl_atomic_int_t;
typedef int64_t cdsl_atomic_int64_t;
#define CDSL_ATOMIC_LOAD(p) (*(p))
#define CDSL_ATOMIC_STORE(p, v) (*(p) = (v))
#define CDSL_ATOMIC_FETCH_ADD(p, v) (*(p) += (v), *(p) - (v))
#define CDSL_ATOMIC_FETCH_SUB(p, v) (*(p) -= (v), *(p) + (v))
#else
#include <stdatomic.h>
typedef _Atomic long cdsl_atomic_long_t;
typedef _Atomic int cdsl_atomic_int_t;
typedef _Atomic int64_t cdsl_atomic_int64_t;
#define CDSL_ATOMIC_LOAD(p) atomic_load(p)
#define CDSL_ATOMIC_STORE(p, v) atomic_store(p, v)
#define CDSL_ATOMIC_FETCH_ADD(p, v) atomic_fetch_add(p, v)
#define CDSL_ATOMIC_FETCH_SUB(p, v) atomic_fetch_sub(p, v)
#endif

#endif /* CDSL_PORTABILITY_H */
