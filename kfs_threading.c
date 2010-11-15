/**
 * Abstraction layer for multi-threading directives.
 *
 * Strategy for this file is, for now, mere 1-on-1 mirror of pthreads, growing
 * to mirror functionality as needed. Big TODO is proper error checking (and
 * handling?). For now, all threading code that causes pthreads to return an
 * error immediately crashes the entire program.
 */

#include "kfs_threading.h"

#include <pthread.h>

#include "kfs_logging.h"

inline void
kfs_rwlock_readlock(kfs_rwlock_t *lock)
{
    int ret = 0;

    KFS_ASSERT(lock != NULL);
    ret = pthread_rwlock_rdlock(lock);
    KFS_ASSERT(ret == 0);

    return;
}

inline void
kfs_rwlock_writelock(kfs_rwlock_t *lock)
{
    int ret = 0;

    KFS_ASSERT(lock != NULL);
    ret = pthread_rwlock_wrlock(lock);
    KFS_ASSERT(ret == 0);

    return;
}

inline void
kfs_rwlock_unlock(kfs_rwlock_t *lock)
{
    int ret = 0;

    KFS_ASSERT(lock != NULL);
    ret = pthread_rwlock_unlock(lock);
    KFS_ASSERT(ret == 0);

    return;
}

inline int
kfs_rwlock_init(kfs_rwlock_t *lock)
{
    int ret = 0;

    KFS_ASSERT(lock != NULL);
    ret = pthread_rwlock_init(lock, NULL);

    return ret;
}

inline void
kfs_rwlock_destroy(kfs_rwlock_t *lock)
{
    int ret = 0;

    KFS_ASSERT(lock != NULL);
    ret = pthread_rwlock_destroy(lock);
    KFS_ASSERT(ret == 0);

    return;
}
