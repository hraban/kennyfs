#ifndef KFS_LOADBRICK_H
#define KFS_LOADBRICK_H

#include <fuse.h>

struct kfs_loadbrick {
    const struct fuse_operations *oper;
    /** Session data used to keep track of (and free) resources. */
    void *priv;
};

int get_root_brick(const char *conffile, struct kfs_loadbrick *brick);
void del_root_brick(struct kfs_loadbrick *brick);

#endif
