#ifndef KFS_LOADBRICK_H
#define KFS_LOADBRICK_H

#include <fuse.h>

struct kfs_loadbrick {
    const struct kfs_operations *oper;
    /** State of the brick as returned by init(). */
    void *private_data;
    /** Session data used to keep track of (and free) resources. */
    void *_lbpriv;
};

int get_root_brick(const char *conffile, struct kfs_loadbrick *brick);
struct kfs_loadbrick * del_root_brick(struct kfs_loadbrick *brick);

#endif
