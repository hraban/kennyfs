#ifndef KFS_LOADBRICK_H
#define KFS_LOADBRICK_H

#include "kfs_api.h"

struct kfs_brick_api * get_root_brick(const char *conffile);
void del_root_brick(struct kfs_brick_api *brick_api);

#endif
