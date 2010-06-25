#ifndef KENNYFS_NETWORK_SERVER_H
#define KENNYFS_NETWORK_SERVER_H

#include <stdlib.h>
#include "kfs.h"

/**
 * Node in a linked list of connected network clients.
 */
struct client_node {
    /*
     * Linked list pointers for list of all connected clients.
     */
    struct client_node *next;
    struct client_node *prev;
    /*
     * Network I/O buffers.
     */
    /** Start of the buffer for received, non-processed chars. */
    char *readbuf_start;
    /** First of received chars that need processing. */
    char *readbuf_head;
    /** End of the buffer for received, non-processed chars, + 1. */
    char *readbuf_end;
    /** Start of the buffer for chars that need to be sent to client.. */
    char *writebuf_start;
    /** First of the chars that need to be sent to client. */
    char *writebuf_head;
    /** End of the buffer for chars that need to be sent to client, + 1. */
    char *writebuf_end;
    /** Number of received chars that need processing. */
    size_t readbuf_used;
    /** Number of chars that need to be sent to client. */
    size_t writebuf_used;
    /*
     * Misc elements.
     */
    /** Size of the operation currently being received. 0 if none pending. */
    size_t opsize;
    int sockfd;
    /** Set to true once a client is recognized as speaking the protocol. */
    uint_t got_sop;
};

typedef struct client_node *client_t;

#endif
