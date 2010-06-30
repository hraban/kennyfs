#ifndef KFS_TCP_BRICK_CONNECTION_H
#define KFS_TCP_BRICK_CONNECTION_H

#include <stddef.h>

#include "tcp_brick/kfs_brick_tcp.h"

int init_connection(const struct kfs_brick_tcp_arg *conf);
int do_operation(char *resbuf, size_t resbufsize,
                 const char *operbuf, size_t operbufsize);

#endif
