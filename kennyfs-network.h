#ifndef KFS_NETWORK_H
#define KFS_NETWORK_H

/** The start of the protocol: sent whenever a new client connects. */
#define SOP_STRING "poep\n"

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
    KFS_OPID_DESTROY,
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
