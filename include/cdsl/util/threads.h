/**
 * @file threads.h
 * @brief Simple portability layer for threads and mutexes.
 */

#ifndef CDSL_UTIL_THREADS_H
#define CDSL_UTIL_THREADS_H

#ifdef _WIN32
#include <windows.h>
#include <process.h>

typedef HANDLE cdsl_thread_t;
typedef CRITICAL_SECTION cdsl_mutex_t;
typedef SRWLOCK cdsl_rwlock_t;

#define CDSL_MUTEX_INIT(m) InitializeCriticalSection(m)
#define CDSL_MUTEX_LOCK(m) EnterCriticalSection(m)
#define CDSL_MUTEX_UNLOCK(m) LeaveCriticalSection(m)
#define CDSL_MUTEX_DESTROY(m) DeleteCriticalSection(m)

#define CDSL_RWLOCK_INIT(l) InitializeSRWLock(l)
#define CDSL_RWLOCK_RDLOCK(l) AcquireSRWLockShared(l)
#define CDSL_RWLOCK_WRLOCK(l) AcquireSRWLockExclusive(l)
#define CDSL_RWLOCK_UNLOCK_RD(l) ReleaseSRWLockShared(l)
#define CDSL_RWLOCK_UNLOCK_WR(l) ReleaseSRWLockExclusive(l)
#define CDSL_RWLOCK_DESTROY(l) ((void)0)

#define CDSL_RWLOCK_INITIALIZER SRWLOCK_INIT

typedef INIT_ONCE cdsl_once_t;
#define CDSL_ONCE_INIT INIT_ONCE_STATIC_INIT

/* Inline wrapper for InitOnceExecuteOnce to match void(void) signature */
static inline BOOL CALLBACK cdsl_internal_once_wrapper(PINIT_ONCE once, PVOID param, PVOID *context) {
    (void)once; (void)context;
    void (*fn)(void) = (void (*)(void))param;
    if (fn) fn();
    return TRUE;
}
#define CDSL_ONCE_RUN(once, fn) InitOnceExecuteOnce(once, cdsl_internal_once_wrapper, (PVOID)fn, NULL)

#define CDSL_THREAD_CREATE(t, fn, arg) (*(t) = (HANDLE)_beginthreadex(NULL, 0, (unsigned (__stdcall *)(void *))fn, arg, 0, NULL), (*(t) == NULL))
#define CDSL_THREAD_JOIN(t) (WaitForSingleObject(t, INFINITE), CloseHandle(t))

#else
#include <pthread.h>

typedef pthread_t cdsl_thread_t;
typedef pthread_mutex_t cdsl_mutex_t;
typedef pthread_rwlock_t cdsl_rwlock_t;

#define CDSL_MUTEX_INIT(m) pthread_mutex_init(m, NULL)
#define CDSL_MUTEX_LOCK(m) pthread_mutex_lock(m)
#define CDSL_MUTEX_UNLOCK(m) pthread_mutex_unlock(m)
#define CDSL_MUTEX_DESTROY(m) pthread_mutex_destroy(m)

#define CDSL_RWLOCK_INIT(l) pthread_rwlock_init(l, NULL)
#define CDSL_RWLOCK_RDLOCK(l) pthread_rwlock_rdlock(l)
#define CDSL_RWLOCK_WRLOCK(l) pthread_rwlock_wrlock(l)
#define CDSL_RWLOCK_UNLOCK_RD(l) pthread_rwlock_unlock(l)
#define CDSL_RWLOCK_UNLOCK_WR(l) pthread_rwlock_unlock(l)
#define CDSL_RWLOCK_DESTROY(l) pthread_rwlock_destroy(l)

#define CDSL_RWLOCK_INITIALIZER PTHREAD_RWLOCK_INITIALIZER

typedef pthread_once_t cdsl_once_t;
#define CDSL_ONCE_INIT PTHREAD_ONCE_INIT
#define CDSL_ONCE_RUN(once, fn) pthread_once(once, fn)

#define CDSL_THREAD_CREATE(t, fn, arg) pthread_create(t, NULL, (void *(*)(void *))fn, arg)
#define CDSL_THREAD_JOIN(t) pthread_join(t, NULL)

#endif

#endif /* CDSL_UTIL_THREADS_H */
