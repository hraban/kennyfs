#ifndef KENNYFS_NETWORK_SERVER_HANDLERS_H
#define KENNYFS_NETWORK_SERVER_HANDLERS_H

#define FUSE_USE_VERSION 26

#include <fuse.h>
#include <fuse_opt.h>
#include "network-server/server.h"

typedef int (* handler_t)(client_t c, const char *rawop, size_t opsize);

void init_handlers(const struct fuse_operations *kenny_oper);
const handler_t * get_handlers(void);

#undef FUSE_USE_VERSION

#endif
