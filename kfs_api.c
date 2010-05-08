/**
 * Functions and datatypes that aid in the communication with and between
 * different parts of a KennyFS system, called ``bricks''.
 */

#include "kfs_api.h"

#include <errno.h>
#include <string.h>

#include "kfs.h"
#include "kfs_logging.h"
#include "kfs_memory.h"
#include "kfs_misc.h"

/**
 * Create an identifier for a brick. Copies the string by reference, it should
 * not be freed while this identifier is still in use. Returns NULL on error, a
 * freshly allocated struct on success.
 */
struct kfs_brick_passport *
kfs_brick_makepassport(char *sopath)
{
    struct kfs_brick_passport *passp = NULL;

    KFS_ASSERT(sopath != NULL);
    passp = KFS_MALLOC(sizeof(*passp));
    if (passp == NULL) {
        KFS_ERROR("Creating passport failed.");
    } else {
        passp->sopath = sopath;
    }

    return passp;
}

/**
 * Clean up a brick identifier.
 */
struct kfs_brick_passport *
kfs_brick_delpassport(struct kfs_brick_passport *passp)
{
    KFS_ASSERT(passp != NULL && passp->sopath != NULL);
    passp->sopath = KFS_FREE(passp->sopath);
    passp = KFS_FREE(passp);

    return passp;
}

/**
 * Initialize a new argument struct. Returns NULL on error, pointer to struct on
 * success. Must eventually be cleaned up. Does not allocate memory for the
 * payload but keeps a reference to its target---do not reuse this struct after
 * freeing that. The cleanup function does, however, free that memory, if it is
 * not NULL.
 *
 * Intended use for this function is by wrapper functions a la
 * `kfs_brick_foo_makearg(...)'.
 */
struct kfs_brick_arg *
kfs_brick_makearg(char *payload, size_t payload_size)
{
    struct kfs_brick_arg *arg = NULL;

    /* Either they both are zero, or they both are not: */
    KFS_ASSERT(!xor(payload == NULL, payload_size == 0));
    arg = KFS_MALLOC(sizeof(*arg));
    if (arg == NULL) {
        KFS_ERROR("malloc: %s", strerror(errno));
    } else {
        arg->payload = payload;
        arg->payload_size = payload_size;
        arg->next_bricks = NULL;
        arg->num_next_bricks = 0;
        arg->next_args = NULL;
    }

    return arg;
}

/**
 * Clean up an arg struct and all its elements. If the payload pointer is not
 * NULL, it is also freed.
 */
struct kfs_brick_arg *
kfs_brick_delarg(struct kfs_brick_arg *arg)
{
    size_t i = 0;

    KFS_ASSERT(arg != NULL);
    for (i = 0; i < arg->num_next_bricks; i++) {
        KFS_ASSERT(arg->next_bricks != NULL && arg->next_bricks[i] != NULL);
        arg->next_bricks[i] = KFS_FREE(arg->next_bricks[i]);
        KFS_ASSERT(arg->next_args != NULL && arg->next_args[i] != NULL);
        arg->next_args[i] = KFS_FREE(arg->next_args[i]);
    }
    if (i > 0) {
        arg->next_bricks = KFS_FREE(arg->next_bricks);
        arg->next_args = KFS_FREE(arg->next_args);
    }
    if (arg->payload != NULL) {
        KFS_ASSERT(arg->payload_size != 0);
        arg->payload = KFS_FREE(arg->payload);
    } else {
        KFS_ASSERT(arg->payload_size == 0);
    }
    arg = KFS_FREE(arg);

    return arg;
}

/**
 * Add another brick to connect to. Unlike the initialization functions, these
 * arguments are copied by reference, not by value, and thus should not be freed
 * after this function returns. They *will* be freed when the entire argument is
 * freed, and, thus, should be properly allocated (not read-only).
 */
int
kfs_brick_addnext(
        struct kfs_brick_arg *arg,
        struct kfs_brick_passport *next_brick,
        struct kfs_brick_arg *next_arg)
{
    size_t allocsize = 0;
    int ret = 0;

    KFS_ASSERT(arg != NULL && arg->next_bricks != NULL);
    KFS_ASSERT(next_brick != NULL && next_arg != NULL);
    /* TODO: Optimize memory allocation strategy. */
    arg->num_next_bricks += 1;
    allocsize = sizeof(arg->next_bricks[0]) * (arg->num_next_bricks);
    arg->next_bricks = KFS_REALLOC(arg->next_bricks, allocsize);
    if (arg->next_bricks != NULL) {
        allocsize = sizeof(arg->next_args[0]) * (arg->num_next_bricks);
        arg->next_args = KFS_REALLOC(arg->next_args, allocsize);
        if (arg->next_args == NULL) {
            arg->next_bricks = KFS_FREE(arg->next_bricks);
            arg->next_bricks = NULL;
        }
    }
    if (arg->next_bricks == NULL) {
        /* TODO: Properly handle this. */
        KFS_ERROR("Not enough memory to add new brick arg in chain.");
        ret = -1;
    } else {
        arg->next_bricks[arg->num_next_bricks - 1] = next_brick;
        arg->next_args[arg->num_next_bricks - 1] = next_arg;
        ret = 0;
    }

    return ret;
}
