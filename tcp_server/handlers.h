#ifndef KFS_TCP_SERVER_HANDLERS_H
#define KFS_TCP_SERVER_HANDLERS_H

#include <fuse.h>
#include "tcp_server/server.h"

typedef int (* handler_t)(client_t c, const char *rawop, size_t opsize);

void init_handlers(const struct fuse_operations *kenny_oper);
const handler_t * get_handlers(void);

#endif
