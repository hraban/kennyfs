/**
 * Abstraction layer for multi-threading directives.
 *
 * Strategy for this file is, for now, mere 1-on-1 mirror of pthreads, growing
 * to mirror functionality as needed. Big TODO is proper error checking (and
 * handling?). For now, all threading code that causes pthreads to return an
 * error immediately crashes the entire program.
 */

#include "kfs_threading.h"

#include <string.h>
#include <pthread.h>

#include "kfs_logging.h"

/**
 * Abort operation if given return value is not 0.
 */
static inline void
work_or_die(int ret)
{
    if (ret != 0) {
        KFS_ABORT("Encountered unrecoverable threading error: %s.\n",
                strerror(ret));
    }

    return;
}

void
kfs_rwlock_readlock(kfs_rwlock_t *lock)
{
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(lock != NULL);
    ret = pthread_rwlock_rdlock(lock);
    work_or_die(ret);

    KFS_RETURN();
}

void
kfs_rwlock_writelock(kfs_rwlock_t *lock)
{
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(lock != NULL);
    ret = pthread_rwlock_wrlock(lock);
    work_or_die(ret);

    KFS_RETURN();
}

void
kfs_rwlock_unlock(kfs_rwlock_t *lock)
{
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(lock != NULL);
    ret = pthread_rwlock_unlock(lock);
    work_or_die(ret);

    KFS_RETURN();
}

int
kfs_rwlock_init(kfs_rwlock_t *lock)
{
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(lock != NULL);
    ret = pthread_rwlock_init(lock, NULL);

    KFS_RETURN(ret);
}

void
kfs_rwlock_destroy(kfs_rwlock_t *lock)
{
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(lock != NULL);
    ret = pthread_rwlock_destroy(lock);
    work_or_die(ret);

    KFS_RETURN();
}
