#include <stdlib.h>

#include "kfs.h"
#include "kfs_logging.h"
#include "kfs_memory.h"

/**
 * Minimalistic wrapper around syscall free(). TODO: mangle the entire array to
 * catch bugs (accessing free()d memory) early.
 */
inline void *
kfs_free(void *p)
{
    KFS_ASSERT(p != NULL);
    free(p);
    return NULL;
}

/**
 * Minimalistic wrapper around syscall realloc(). TODO: mangle the entire array
 * to catch bugs (accessing free()d memory) early in case of relocation. Perhaps
 * even a debug mode where relocation (and mangling) is forced.
 */
inline void *
kfs_realloc(void *p, size_t size)
{
    KFS_ASSERT(p != NULL);
    return realloc(p, size);
}
