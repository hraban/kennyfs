#ifndef KFS_API_H
#define KFS_API_H

/* Typedef size_t is needed. */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
/* In C99 stddef.h is a legal (lighter) substitute to get size_t. */
#  include "stddef.h"
#else
#  include "stdlib.h"
#endif

#include "kfs.h"

/**
 * All the necessary information to load a brick. Under linux this is just the
 * path to a dynamically loadable .so library, but I have no idea what other
 * platforms / the future will give.
 */
struct kfs_brick_passport {
    char *sopath;
};

struct kfs_brick_passport * kfs_brick_makepassport(char *sopath);
struct kfs_brick_passport * kfs_brick_delpassport(
        struct kfs_brick_passport *passp);

/**
 * Information that a brick needs for setup. This structure not only holds the
 * arguments for the brick itself but provides in information that brick needs
 * to, in turn, connect to its underlying brick as well, and their arguments,
 * and so forth. The actual argument data for this brick is serialized into an
 * array of characters for two reasons:
 * - Type safety: no casting required.
 * - Serializability: intermediate bricks can safely transmit this entire struct
 *   over any transport layer because all element datatypes are known.
 * This means that every brick needs to unserialize its argument list before it
 * can use it.
 */
struct kfs_brick_arg {
    /* Serialized argument data. */
    size_t payload_size;
    char *payload;
    /* The sublying bricks to connect to, if any. */
    size_t num_next_bricks;
    struct kfs_brick_passport **next_bricks;
    /* Their respective arguments. */
    struct kfs_brick_arg **next_args;
};

/*
 * Functions that help in managing the above struct.
 */
/**
 * This function creates a fresh general brick from a pre-serialized brick. Not
 * very useful for frontends: intended for use in a brick's custom
 * kfs_brick_xxx_makearg().
 */
struct kfs_brick_arg * kfs_brick_makearg(char *payload, size_t payload_size);
struct kfs_brick_arg * kfs_brick_delarg(struct kfs_brick_arg *arg);
int kfs_brick_addnext(
        struct kfs_brick_arg *arg,
        struct kfs_brick_passport *next_brick,
        struct kfs_brick_arg *next_arg);

/*
 * The following four typedefs are only for the readability of the declaration
 * of struct kfs_brick_api and should not be used directly. Use that struct and
 * kfs_brick_getapi_f intead.
 */
/**
 * Function that constructs a general argument struct. Returns a pointer to such
 * a struct which can later be passed to member .init(). General argument
 * structs can be freed with kfs_brick_delarg().
 */
typedef struct kfs_brick_arg * (* kfs_brick_makearg_f)();
/**
 * Function that prepares the brick for operation. N.B.: The scope of the
 * argument is the duration of the init function. If persistance is needed, the
 * callee must allocate (and free!) it. The caller can (and should) free the
 * argument struct after the call to the init function returns.
 */
typedef int (* kfs_brick_init_f)(struct kfs_brick_arg *arg);
/** Function to obtain pointer to FUSE callback handlers for this brick. */
typedef const struct fuse_operations * (* kfs_brick_getfuncs_f)(void);
/** Function that shuts down the brick (at least until the next init()). */
typedef void (* kfs_brick_halt_f)(void);

struct kfs_brick_api {
    kfs_brick_makearg_f makearg;
    kfs_brick_init_f init;
    kfs_brick_getfuncs_f getfuncs;
    kfs_brick_halt_f halt;
};

/**
 * Function that gets API (above three) of a brick. Every KennyFS brick should
 * provide a public function of this type.
 */
typedef const struct kfs_brick_api * (* kfs_brick_getapi_f)(void);

#endif /* _KFS_API_H */
