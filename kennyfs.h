#ifndef _KENNYFS_H
#define _KENNYFS_H

#include <assert.h>
#include "kfs_logging.h"

#define KFS_ASSERT assert
/* Memory managament is done by the OS. */
#define KFS_MALLOC malloc
#define KFS_FREE kfs_free

static void *
kfs_free(void *p)
{
    KFS_ASSERT(p != NULL);
    free(p);
    return NULL;
}

#endif
