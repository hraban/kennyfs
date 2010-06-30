#ifndef KFS_TCP_BRICK_HANDLERS_H
#define KFS_TCP_BRICK_HANDLERS_H

#include <fuse.h>

const struct fuse_operations * get_handlers(void);

#endif
