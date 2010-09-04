#ifndef KFS_NETWORK_H
#define KFS_NETWORK_H

/*
 * The KennyFS TCP brick and -server share this header.
 *
 * The communication protocol can be described as follows:
 *
 * - When a client connects to a server, both send the same SOP (start of
 *   protocol) string to verify protocol conformance. The server behaves
 *   asynchronously during this step, meaning it can either receive the string
 *   first or send it out first, depending on the client.
 * - From here on, synchronous messaging starts with the client sending an
 *   operation and the server replying with an answer.
 *
 * An operation (client to server) is built up like this:
 *
 * - Size of the serialised operation as a uint32_t (4 bytes).
 * - ID of the operation as a uint16_t (2 bytes).
 * - Serialised operation (n bytes).
 *
 * A reply (server to client) is built up like this:
 *
 * - Return value as a uint32_t (4 bytes).
 * - Size of the body of the reply as a uint32_t (4 bytes).
 * - The body of the reply, if any.
 * 
 * TODO: update documentation about return value (iirc, it is cast from int to a
 * uint32_t and then back to int).
 *
 * Note that there is NO authentication and NO encryption, so please only start
 * this in a trusted environment. All network operations are non-blocking but
 * all operations are blocking (i.e.: one slow client will not clog the server
 * but one client requesting something from a slow drive will).
 *
 * Explanation about the serialized form of each operation can be found in the
 * TCP brick server documentation / comments.
 */


/** The start of the protocol: sent whenever a new client connects. */
#define SOP_STRING "poep\x0a"
/**
 * Messages between server and client are guaranteed to never exceed this
 * value. This helps in detecting corrupted message headers containing (part of
 * the) message length in bytes.
 *
 * TODO: Make the server honour this limit as well.
 */
#define MAX_MESSAGE_LEN (1 << 20)

/**
 * Identifiers for fuse operations.
 */
enum fuse_op_id {
    KFS_OPID_GETATTR,
    KFS_OPID_READLINK,
    KFS_OPID_MKNOD,
    KFS_OPID_MKDIR,
    KFS_OPID_UNLINK,
    KFS_OPID_RMDIR,
    KFS_OPID_SYMLINK,
    KFS_OPID_RENAME,
    KFS_OPID_LINK,
    KFS_OPID_CHMOD,
    KFS_OPID_CHOWN,
    KFS_OPID_TRUNCATE,
    KFS_OPID_UTIME,
    KFS_OPID_OPEN,
    KFS_OPID_READ,
    KFS_OPID_WRITE,
    KFS_OPID_STATFS,
    KFS_OPID_FLUSH,
    KFS_OPID_RELEASE,
    KFS_OPID_FSYNC,
    KFS_OPID_SETXATTR,
    KFS_OPID_GETXATTR,
    KFS_OPID_LISTXATTR,
    KFS_OPID_REMOVEXATTR,
    KFS_OPID_OPENDIR,
    KFS_OPID_READDIR,
    KFS_OPID_RELEASEDIR,
    KFS_OPID_FSYNCDIR,
    KFS_OPID_ACCESS,
    KFS_OPID_CREATE,
    KFS_OPID_FTRUNCATE,
    KFS_OPID_FGETATTR,
    KFS_OPID_LOCK,
    KFS_OPID_UTIMENS,
    KFS_OPID_BMAP,
    KFS_OPID_IOCTL,
    KFS_OPID_POLL,
    KFS_OPID_QUIT,
    KFS_OPID_MAX_
};

#endif
