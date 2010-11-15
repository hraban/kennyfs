#ifndef KFS_THREADING_H
#define KFS_THREADING_H

#include <pthread.h>

#define KFS_RWLOCK_INITIALIZER PTHREAD_RWLOCK_INITIALIZER

typedef pthread_rwlock_t kfs_rwlock_t;

inline void kfs_rwlock_readlock(kfs_rwlock_t *lock);
inline void kfs_rwlock_writelock(kfs_rwlock_t *lock);
inline void kfs_rwlock_unlock(kfs_rwlock_t *lock);
inline int kfs_rwlock_init(kfs_rwlock_t *lock);
inline void kfs_rwlock_destroy(kfs_rwlock_t *lock);
/* TODO: kfs_rwlock_islocked()? */

#endif
