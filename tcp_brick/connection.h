#ifndef KFS_TCP_BRICK_CONNECTION_H
#define KFS_TCP_BRICK_CONNECTION_H

#include <stddef.h>

#include "tcp_brick/kfs_brick_tcp.h"

int init_connection(const struct kfs_brick_tcp_arg *conf);
int do_operation(const char *operbuf, const size_t *operbufsize,
                 char *resbuf, size_t *resbufsize, int *serverret);

#endif
