#ifndef KFS_THREADING_H
#define KFS_THREADING_H

#include <pthread.h>

#define KFS_RWLOCK_INITIALIZER PTHREAD_RWLOCK_INITIALIZER

typedef pthread_rwlock_t kfs_rwlock_t;
typedef pthread_t kfs_threadid_t;

void kfs_rwlock_readlock(kfs_rwlock_t *lock);
void kfs_rwlock_writelock(kfs_rwlock_t *lock);
void kfs_rwlock_unlock(kfs_rwlock_t *lock);
int kfs_rwlock_init(kfs_rwlock_t *lock);
void kfs_rwlock_destroy(kfs_rwlock_t *lock);
/* TODO: kfs_rwlock_islocked()? */
kfs_threadid_t kfs_getthreadid(void);

#endif
