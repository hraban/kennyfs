#ifndef KFS_MEMORY_H
#define KFS_MEMORY_H

/* TODO: Is there a lighter header file for malloc/free? */
#include <stdlib.h>

/* Memory managament is done by the OS. */
#ifndef KFS_CALLOC
#  define KFS_CALLOC calloc
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


inline void * kfs_malloc(size_t s);
inline void * kfs_free(void *p);
inline void * kfs_realloc(void *p, size_t size);

#endif
