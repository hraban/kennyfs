#ifndef KFS_TCP_BRICK_CONNECTION_H
#define KFS_TCP_BRICK_CONNECTION_H

#include <pthread.h>
#include <stddef.h>

#include "tcp_brick/kfs_brick_tcp.h"
#include "tcp_brick/tcp_brick.h"

/** Information needed to connect to the server. */
struct conn_info {
    /** Hostname of the server. */
    const char *hostname;
    /** Service to connect to. */
    const char *port;
};

/**
 * Operation passed from fuse handler to connection thread.
 */
struct serialised_operation {
    enum fuse_op_id id;
    char *operbuf;
    size_t operbufsize;
    char *resbuf;
    size_t resbufsize;
    size_t resbufused;
    int serverret;
    int clientret;
    /* Signal the caller that the operation is done. */
    pthread_mutex_t done_mutex;
    pthread_cond_t done_cond;
};

int init_connection(const struct conn_info *conf);
int do_operation(struct serialised_operation *arg);

#endif
