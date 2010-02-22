#ifndef _KENNYFS_H
#define _KENNYFS_H

/*
 * This header file requires the following header files:
 * #include <fuse.h>
 * #include <stdio.h>
 * #include <stdlib.h>
 */

#define KFS_VERSION "0.0"

#include <assert.h>
#include "kfs_logging.h"

#define KFS_ASSERT assert
/* Memory managament is done by the OS. */
#define KFS_MALLOC malloc
#define KFS_FREE kfs_free

/*
 * Types and function declarations.
 */

/* Function that prepares the backend for operation. Argument list is
 * backend-dependent.
 */
typedef int (* kfs_backend_init_f)();
/* Function to obtain the FUSE callback handlers for this backend. */
typedef struct fuse_operations (* kfs_backend_getfuncs_f)(void);
/* Function that shuts down the backend (at least until the next init()). */
typedef void (* kfs_backend_halt_f)(void);

struct kfs_backend_api {
    kfs_backend_init_f init;
    kfs_backend_getfuncs_f getfuncs;
    kfs_backend_halt_f halt;
};

/* Function that gets API (above three) of a backend. */
typedef struct kfs_backend_api (* kfs_backend_getapi_f)(void);

/*
 * Small functions that will probably be inlined.
 */

static inline void *
kfs_free(void *p)
{
    KFS_ASSERT(p != NULL);
    free(p);
    return NULL;
}

#endif
