#ifndef KFS_MEMORY_H
#define KFS_MEMORY_H

#include <stdlib.h>

#ifndef KFS_CALLOC
#  define KFS_CALLOC kfs_calloc
#endif
#ifndef KFS_MALLOC
#  define KFS_MALLOC kfs_malloc
#endif
#ifndef KFS_FREE
#  define KFS_FREE kfs_free
#endif
#ifndef KFS_REALLOC
#  define KFS_REALLOC kfs_realloc
#endif


inline void * kfs_calloc(size_t n, size_t s);
inline void * kfs_malloc(size_t s);
inline void * kfs_free(void *p);
inline void * kfs_realloc(void *p, size_t size);

#endif
