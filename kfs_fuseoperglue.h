#ifndef KFS_FUSEOPERGLUE_H
#define KFS_FUSEOPERGLUE_H

#include <fuse.h>

#include "kfs_api.h"

const struct fuse_operations * kfs2fuse_operations(const struct kfs_operations
        *kfs_oper, void *root_private_data);
const struct fuse_operations * kfs2fuse_clean(const struct fuse_operations *o);

#endif
