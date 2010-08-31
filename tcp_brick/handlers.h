#ifndef KFS_TCP_BRICK_HANDLERS_H
#define KFS_TCP_BRICK_HANDLERS_H

#include <fuse.h>

int init_handlers(void);
const struct kfs_operations * get_handlers(void);

#endif
