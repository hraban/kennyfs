#include "kfs_memory.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "kfs.h"
#include "kfs_logging.h"

/**
 * Minimalistic wrapper around syscall malloc().
 */
inline void *
kfs_malloc(size_t s)
{
    void *p = NULL;

    KFS_ASSERT(s >= 0);
    p = malloc(s);
    if (p == NULL) {
        KFS_ERROR("malloc: %s", strerror(errno));
    } else {
        KFS_DEBUG("Allocated %lu bytes of memory at %p.", (unsigned long) s, p);
    }

    return p;
}

/**
 * Minimalistic wrapper around syscall calloc().
 */
inline void *
kfs_calloc(size_t n, size_t s)
{
    void *p = NULL;

    KFS_ASSERT(s >= 0);
    p = calloc(n, s);
    if (p == NULL) {
        KFS_ERROR("calloc: %s", strerror(errno));
    } else {
        KFS_DEBUG("Allocated and zeroed %lu bytes of memory at %p.", (unsigned
                    long) s, p);
    }

    return p;
}

/**
 * Minimalistic wrapper around syscall free(). TODO: mangle the entire array to
 * catch bugs (accessing free()d memory) early. Another nice test would be
 * strict checking of arguments with values ever returned by kfs_malloc.
 */
inline void *
kfs_free(void *p)
{
    KFS_DEBUG("Freeing %p.", p);
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
    void *newp = NULL;

    KFS_ASSERT(p != NULL);
    newp = realloc(p, size);
    if (newp == NULL) {
        KFS_ERROR("realloc: %s", strerror(errno));
    } else {
        KFS_DEBUG("Reallocated %p to %p, now %lu bytes.", p, newp, (unsigned
                    long) size);
    }

    return newp;
}
