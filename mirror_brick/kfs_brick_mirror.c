/**
 * KennyFS brick that mirrors all operations to all of its subvolumes.
 * 
 *
 * == STATE (aka GLOBALS) ==
 *
 * This brick has two types of states:
 *
 * - Global state, valid during the entire lifetime of this brick. There will be
 *   multiple bricks with separate global states using the same code (different
 *   subvolumes can have the same brick type, being this one). This is the
 *   struct mirror_state.
 *
 * - Session state, also known as the filehandle, is concerned with what happens
 *   to a file (or dir) from open(dir)() to release(dir)(), and in between. This
 *   will always contain a list of one or more bricks that were selected for
 *   this session: operation handlers (can) assume that data is properly mirrored
 *   amongst subvolumes and do not need to contact /all/ subvolumes on read-only
 *   operations. On write operations, however, this is ignored and all
 *   subvolumes are simultaneously updated. This is the struct mirror_fh.
 *
 * Upon initialisation of the brick, a global state is built. It is the
 * responsibility of the user of this brick to pass that state pointer to every
 * operation handler.
 *
 * Whenever a new session is started (open/opendir) a new session state is
 * created. The pointer is disguised as a filehandle and returned: the caller
 * will pass this pointer to every operation handler for this session. 
 *
 *
 * == THREADING / LOCKING / HUNGARIAN NOTATION ==
 *
 * There is a custom naming convention for functions (and macros) that accept a
 * pointer to the global state. Hungarian notation is used for two types of
 * callables (macros, being all-caps, get two underscores):
 *
 * - C_*: only accesses members of the C member (thread-safe, constants).
 *
 * - L_*: accesses the L member without locking (requires lock before call).
 *
 * Everything else will take care of its own locking. Ideally, operation
 * handlers never touch anything prefixed with L_: all access to such data goes
 * through helper functions. This has two benefits: it limits the scope of
 * thread-related source-code and it limits the number of function boundaries
 * that locks cross. By the latter I mean that it is easy to get confused by
 * locks being held by callers: if you access critical data without lock, you
 * must ensure that ALL callers have acquired the lock, but if you acquire the
 * lock yourself, ANY caller that already has the lock will lead your
 * acquisition to deadlock.
 *
 * Note: the `L'- and `C'-members refer to the nested structs in the global
 * state struct, see its definition for an elaboration.
 *
 * The combination of threading and the notion of active vs. inactive bricks
 * introduces a group of very tenacious bugs. In theory, this brick should have
 * a requirement that reads: "after a brick enters a state where
 * it can no longer be considered active, no more operations should be carried
 * out on that brick." Note that this is not exactly the same as "don't touch a
 * brick after it was labeled inactive", which would be hard enough to satisfy
 * already. The ideal case, however, would make the stop on operations go into
 * effect as soon as a subvolume encounters a critical error, even before it
 * returns. The only way to satisfy this would be to acquire a
 * subvolume-specific lock before entering its handler and releasing it after
 * error checking and handling. Obviously, this is not worth it: disallowing
 * concurrent access to operation handlers of a subvolume totally defeats the
 * purpose of threading in the first place. Given that we have to wait for the
 * (first) failing operation handler to return, imagine one that fails and then
 * deadlocks. This brick will be none the wiser and other threads will never
 * know about this failure. This is inherent to threaded I/O and hence, the
 * strict requirement becomes more of a guideline: "try to stop accessing a
 * failed brick asap."
 *
 * This results in an approach where handlers get a list of all active
 * subvolumes once (at the beginning) and sequentially go through it. This does
 * widen the gap between checking and using, but it does allow a strict
 * rollback. Furthermore, since that gap is potentially infinite in the absence
 * of a lock, anyway, this "downside" is less absolute than it seems.
 *
 *
 * == ERROR HANDLING ==
 *
 * Most of the code in operation handlers is concerned with maintaining
 * consistency accross subvolumes in the face of errors. This is done by rolling
 * back operations if possible and by deactivating any subvolume that really can
 * not be saved anymore. Typically, operation handlers come in three flavours:
 *
 * - Read only: the only apparent relevance in this subject is inconsistency.
 *   For now, this module just assumes every node is in sync if it is available.
 *   Of course, it would be much nicer to at least allow dynamic detection of
 *   inconsistencies, if not consolidation: TODO.
 *
 * - Add new stuff: creating new files, directories, appending data, etc.
 *   Failure of any subvolume to do this can be handled by simply performing the
 *   complementary operation on the subvolumes where it did succeed. E.g.: brick
 *   i fails to create a new file: for all bricks j where it did work, remove
 *   that file. Disable any brick j for which that fails (not very likely). That
 *   is how most handlers here actually work. To be even more exact, they could
 *   also handle the case where it just so happens that all (or a lot of) bricks
 *   j fail to roll back: it *might* then be a better idea to try to persist the
 *   operation and disable brick i instead. *Might*, because that would require
 *   more inspection: what if the ratio of failing bricks to succeeding ones is
 *   50/50?  And why would all bricks j fail to roll back, anyway? If this
 *   happens it is not unlikely that subsequent operations will also fail. This
 *   could be examined by trying to perform some operations on them and seeing
 *   if that works (not trivial). What do you do if indeed most bricks suddenly
 *   happen to be failing? (To be fair, that last question is not specific to
 *   this part of the code anymore, it is a general question that warrants a
 *   paragraph by itself).
 *
 * - Modify existing stuff: deleting files/directories, truncating files, etc.
 *   Rolling back operations where this fails on some subvolumes and succeeds on
 *   others can be done in (at least) the following ways:
 *
 *   + If all bricks failed, great. If at least one succeeded and one failed,
 *     disable all failing bricks. This is how this module currently works.
 *
 *   + Backup the old state and try to rollback by restoring this on succeeded
 *     bricks, as is done in the `add-new-stuff' case. This is less trivial,
 *     though: race conditions make it much more likely that rolling back fails
 *     for some subvolumes and not for others, increasing merit of more
 *     complicated error handling.
 *
 *   Both courses of action have obvious benefits and drawbacks, but essentially
 *   it means that this is either more work and/or more fragile.
 *
 * As mentioned earlier, an important recurring theme in error handling is what
 * to do when a significant part of the subvolumes seem to be failing beyond
 * possibility of recovery. The definitive answer is that this depends entirely
 * on factors that can not be extracted through the available API. Roughly, this
 * is a matter of availability versus consistency. TODO: this should be a
 * configuration variable (with a sensible, general, default).
 *
 * The general idea that this portrays is that mirroring multiple subvolumes
 * correctly (!), notably including error handling of corner cases, requires a
 * lot of (careful) work, or is sometimes impossible (more precisely:
 * correctness is sometimes relative), even.  However, this is not to say that
 * it should not be done: errors will happen, and handling will have to happen,
 * sooner or later.  If we do not do it, the user will have to: this should be
 * avoided as much as possible. In other words:
 *
 *   THE GOAL IS TO PERFORM AS MUCH ERROR HANDLING AS POSSIBLE, NO MATTER HOW
 *   UNLIKELY THE ERROR.
 *
 * ``Possible'' is defined in this context: ``A course of action can be taken,
 * which is determined to be better than all other possible courses of action.
 * This takes into account all circumstances but MUST take into account that
 * some of those are unkown to the conflict resolution mechanism.'' In other
 * words:
 *
 *   *CORRECT* ERROR HANDLING IS IMPOSSIBLE *IF AND ONLY IF*: based on the
 *   available data, there are at least two possible states of the world where
 *   the best error handling procedures of one state requires actions that would
 *   actually worsen the situation in the other states, and vice-versa.
 *
 * (Compare this with maximal consistent extensions and the notion of
 * completeness of a set of predicates in propositional logic.)
 *
 * The actual point I am trying to make is: closing a bugreport because
 * resolving it would "take too much work" is not OK. Explicitly introducing a
 * bug by not handling a possible error when writing new code is, in fact, OK:
 * software with known (!) bugs is not regarded worse than no software at all
 * (by me).
 *
 *
 * == OPTIMISATIONS ==
 *
 * This section is mostly a collection of optimisation strategies available to
 * this brick that I feel should at least be seriously considered for
 * implementation if correct, worthwile and feasible. As of writing, this brick
 * is merely a minimal proof of concept and the first subvolume is always the
 * first reading brick, etc.
 *
 * For this topic, the same holds as for error checking: if it is not
 * impossible, it should, sooner or later be done. However, this is a far more
 * prudent mantra: readability, compactness, even elegance are very reasonable
 * considerations in not performing an(y) optimisation.
 *
 * Consider two distinct types of operations: read and write. For the latter,
 * this subject is quite straightforward: all subvolumes must be updated about
 * changes to remain consistent, there is little room for creativity.  There is
 * at least some, involving master volumes and synchronising post-return
 * background processes, for example. However, these kinds of optimisations will
 * (at least by myself) not be seriously considered until it is shown that
 * worthwile optimisations can be achieved here but not with optimising wrapper
 * bricks.
 *
 * For read operations, however, consider at least the following obvious
 * optimisation-related topics:
 *
 * - Load balancing: round-robin, weighted, map/reduce, subvolume overload
 *   (short- and long-term), the names say it all.
 *
 * - Subvolume price versus optimising return: every operation performed on a
 *   subvolume comes at a price, this could add up if for every operation a
 *   see-who-is-fastest method is employed among n subvolumes. Assuming that
 *   this cost is known, n can be dynamically balanced according to measured
 *   performance gains.
 *
 * Dynamic vs. static: only decisions that are theoretically undecidable given
 * the observations available to this brick should require static guidance. By
 * contrast, new possibilities to tweak parameters consulted by these decision
 * making mechanisms are not at all considered harmful. Sometimes, a subvolume,
 * no matter how suitable it may seem to an automaton, is just a poor choice.
 * Consider usage-based billing for services such as S3: avoiding reads, no
 * matter how fast, from this type of subvolume could definitely be a nice
 * thing.
 *
 * One particular parameter that I would like to address is chunk-size
 * optimisation: it is not unimaginable that for some operations the time it
 * takes for a given subvolume to complete the operation does not grow linearly
 * with the size of the request and/or response. Consider, for example,
 * tcp_brick::read(). If the internet layer is sufficiently slow, and the
 * server's backend sufficiently fast, it could be that the number of TCP
 * packets required to carry the response is of measurable influence to the
 * latency. This information could be used to decide the number of subvolumes
 * (and which ones) to split a request over, and how to distribute the load.
 *
 * All pretty words aside, let's be real: none of this is likely to happen in
 * the near future. It just warrants a note that this brick is not considered
 * done until at least all these options have been seriously evaluated.
 */

#define FUSE_USE_VERSION 29

#include "cache_brick/kfs_brick_cache.h"

#ifdef __APPLE__
#  include <sys/xattr.h>
#else
#  include <sys/types.h>
#  include <attr/xattr.h>
#endif
#include <attr/xattr.h>
#include <errno.h>
#include <fcntl.h>
#include <fuse.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "kfs.h"
#include "kfs_api.h"
#include "kfs_logging.h"
#include "kfs_misc.h"
#include "kfs_threading.h"

/**
 * Globals for the entire brick, all operations always.
 *
 * This structure uses some hungarian notation to differentiate between three
 * types of elements:
 *
 * - read-only after initialisation (state.C.*) (never modify these post-init!)
 *
 * - require rwlock to read/write (state.L.*)
 *
 * - the rest (state.*), use common sense here (and elsewhere too, if possible)
 *
 * The lock is intended for protection of the L struct only and should not be
 * held accross operations (release and re-acquire).
 */
struct mirror_state {
    /** Elements that should remain constant after initialisation. */
    struct {
        struct kfs_brick *subvols;
        uint_t num_subvols;
    } C;
    /** Acquire .lock before accessing these elements. */
    struct {
        uint_t num_active_subvols;
        /** For every subvolume: 1 if active, 0 if inactive. */
        uint_t *subvol_active;
    } L;
    /** Lock that must be acquired to access/manipulate this struct. */
    kfs_rwlock_t lock;
};
/* Macros get two hungarian underscores for clarity: */
#define L__IS_ACTIVE(state, id) ((state)->L.subvol_active[(id)])

/**
 * State associated with operations on one file/dir between open/release calls.
 *
 * Stores the subvolume(s) that is/are active and selected for this operation.
 * Any subvolume that is detected to be inactive at the start of an operation is
 * removed from the list. In other words: this is most accurate at the end of an
 * operation handler but it could degrade before the next one starts.
 */
struct mirror_fh {
    /* Array of global ids of subvols used in this session. */
    uint_t *subvols_id;
    /* File-handle associated with each subvolume on this session. */
    uint64_t *subvols_fh;
    uint_t num_subvols;
#if 0 // TODO: is this necessary?
    /** Lock that must be acquired to access/manipulate this struct. */
    kfs_rwlock_t lock = KFS_RWLOCK_INITIALIZER;
#endif
};

struct mirror_dirfh {
    struct kfs_brick *subv;
    uint64_t fh;
};

/** Error returned when no subvolumes are available. */
static const int ENOSUBVOLS = ECHILD;

/**
 * Get the number of subvolumes.
 *
 * Only accesses `constant' elements of the state.
 */
static inline uint_t
C_get_num_subvols(struct mirror_state * const state)
{
    KFS_ASSERT(state != NULL);

    return state->C.num_subvols;
}

/**
 * Get the subvolume with the given ID.
 *
 * Only accesses `constant' elements of the state.
 */
static inline struct kfs_brick *
C_get_subvol_by_ID(struct mirror_state * const state, uint_t id)
{
    KFS_ASSERT(state != NULL);
    KFS_ASSERT(id < C_get_num_subvols(state));

    return &(state->C.subvols[id]);
}

/**
 * Returns 1 if given ID corresponds to an active subvolume in this brick.
 */
static uint_t
is_active(struct mirror_state * const state, uint_t id)
{
    uint_t active = 0;

    KFS_ENTER();

    KFS_ASSERT(state != NULL);
    KFS_ASSERT(id < C_get_num_subvols(state));

    kfs_rwlock_readlock(&state->lock);
    active = L__IS_ACTIVE(state, id);
    kfs_rwlock_unlock(&state->lock);

    KFS_RETURN(active);
}

/**
 * Get n subvolumes that are active.
 *
 * Starts at the first brick in the list, goes on until the last.
 *
 * Puts at most n ids of available subvolumes in the ids array The return value
 * is the actual number of subvolume ids stored in the array.
 *
 * Note that this function does not actually 'allocate' any resources, the
 * subvolumes are never notified that they have been selected. This is purely a
 * glorified macro that selects some ids from a pool of available bricks.
 */
static uint_t
get_some_active_subvols(struct mirror_state * const state, uint_t *ids,
        uint_t n)
{
    const uint_t num_subvols = C_get_num_subvols(state);
    uint_t i = 0;
    uint_t id = 0;

    KFS_ENTER();

    KFS_ASSERT(state != NULL);
    KFS_ASSERT(ids != NULL);
    KFS_ASSERT(num_subvols > 0);
    KFS_ASSERT(n <= num_subvols);

    kfs_rwlock_writelock(&state->lock);
    n = MIN(n, state->L.num_active_subvols);
    for (id = 0, i = 0; i < n && id < num_subvols; id++) {
        /* Get the next active subvolume. */
        if (L__IS_ACTIVE(state, id) == 0) {
            continue;
        }
        ids[i] = id;
        i += 1;
    }
    kfs_rwlock_unlock(&state->lock);

    if (i == 0) {
        KFS_ERROR("No more active subvolumes available in this mirror brick!");
    }

    KFS_RETURN(i);
}

/**
 * Get the IDs of all active subvolumes in a freshly allocated array.
 *
 * Do not forget to FREE(). Returns the amount of active subvolumes in the array
 * (0 on error).
 */
static uint_t
get_all_active_subvols(struct mirror_state * const state, uint_t **buf)
{
    const uint_t num_subvols = C_get_num_subvols(state);
    uint_t *ids = NULL;
    uint_t ret = 0;

    KFS_ENTER();

    KFS_ASSERT(num_subvols > 0);
    ids = KFS_MALLOC(num_subvols * sizeof(*ids));
    if (ids == NULL) {
        KFS_RETURN(0);
    }
    ret = get_some_active_subvols(state, ids, num_subvols);
    /* If ret == 0 (no active subvols): realloc(ids, 0) ~= free(ids). */
    KFS_ASSERT(ret <= num_subvols);
    /* Free unused memory. No need for check, given new_size <= old_size. */
    ids = KFS_REALLOC(ids, ret * sizeof(*ids));
    *buf = ids;

    KFS_RETURN(ret);
}

/**
 * No-hassle subvolume getter: just get one, any, active subvolume for reading.
 *
 * Returns a pointer to the subvolume on success, NULL on error. If the idp
 * argument is not NULL, the id of the subvolume is stored in it on succesful
 * return.
 */
static struct kfs_brick *
get_one_reader(struct mirror_state * const state, uint_t *idp)
{
    struct kfs_brick *brick = NULL;
    uint_t *my_idp = NULL;
    uint_t id = 0;
    uint_t n = 0;

    KFS_ENTER();

    if (idp == NULL) {
        my_idp = &id;
    } else {
        my_idp = idp;
    }
    n = get_some_active_subvols(state, my_idp, 1);
    if (n == 0) {
        brick = NULL;
    } else {
        KFS_ASSERT(n == 1);
        KFS_ASSERT(*my_idp <= C_get_num_subvols(state));
        brick = C_get_subvol_by_ID(state, *my_idp);
    }

    KFS_RETURN(brick);
}

static void
eject_subvolume(struct mirror_state * const state, uint_t id)
{
    struct kfs_brick * const subv = C_get_subvol_by_ID(state, id);

    KFS_ENTER();

    kfs_rwlock_writelock(&state->lock);
    /* Maybe some other thread already ejected this volume. */
    if (L__IS_ACTIVE(state, id) == 1) {
        KFS_ASSERT(state->L.num_active_subvols > 0);
        KFS_ERROR("Unable to deal with the errors in subvolume #%u:%s, "
                "resorting to drastic measures: eject from mirror array.",
                id + 1, subv->name);
        L__IS_ACTIVE(state, id) = 0;
        state->L.num_active_subvols -= 1;
        if (state->L.num_active_subvols == 0) {
            KFS_ERROR("No more active subvolumes for this mirror brick.");
        }
    }
    kfs_rwlock_unlock(&state->lock);

    KFS_RETURN();
}

/**
 * Create new freshly allocated file handle, initialised and ready for use.
 *
 * If the num_subvols argument is 0, no initialisation is done, just the
 * filehandle itself is allocated. Returns NULL on memory error. Do not forget
 * to call del_fh() on this!
 */
static struct mirror_fh *
new_fh(uint_t num_subvols)
{
    struct mirror_fh *my_fh = NULL;
    uint64_t *subvols_fh = NULL;
    uint_t *ids = NULL;

    KFS_ENTER();

    KFS_ASSERT(num_subvols > 0);
    my_fh = KFS_MALLOC(sizeof(*my_fh));
    if (my_fh != NULL) {
        subvols_fh = KFS_MALLOC(num_subvols * sizeof(*subvols_fh));
        if (subvols_fh != NULL) {
            my_fh->subvols_fh = subvols_fh;
            ids = KFS_MALLOC(num_subvols * sizeof(*ids));
            if (ids != NULL) {
                my_fh->subvols_id = ids;
                my_fh->num_subvols = num_subvols;
                KFS_RETURN(my_fh);
            }
            subvols_fh = KFS_FREE(subvols_fh);
        }
        my_fh = KFS_FREE(my_fh);
    }

    KFS_RETURN(NULL);
}

/**
 * Clean up and delete filehandle created with new_fh.
 */
static struct mirror_fh *
del_fh(struct mirror_fh *fh)
{
    KFS_ENTER();

    KFS_ASSERT(fh != NULL);
    /* If subvols_... == NULL then num_subvols == 0. */
    KFS_ASSERT(fh->subvols_fh != NULL || fh->num_subvols == 0);
    KFS_ASSERT(fh->subvols_id != NULL || fh->num_subvols == 0);
    if (fh->subvols_fh != NULL) {
        fh->subvols_fh = KFS_FREE(fh->subvols_fh);
    }
    if (fh->subvols_id != NULL) {
        fh->subvols_id = KFS_FREE(fh->subvols_id);
    }
    fh = KFS_FREE(fh);

    KFS_RETURN(fh);
}

/**
 * Lock a (region of) a file.
 *
 * It is specified at the top of this file because other handlers use it.
 * However, it is not implemented (always returns ENOSYS): TODO.
 */
static int
mirror_lock(const kfs_context_t co, const char *path, struct fuse_file_info *fi,
        int cmd, struct flock *lock)
{
    (void) co;
    (void) path;
    (void) fi;
    (void) cmd;
    (void) lock;

    KFS_ENTER();

    KFS_RETURN(-ENOSYS);
}

/**
 * Lock this file or accept pending locks.
 *
 * Used to protect from race conditions in the backup, operate, rollback
 * process, where a concurrent thread might update a file inbetween, causing a
 * rollback to outdated data.
 *
 * Returns 0 when a new lock is acquired, 1 when an existing lock was
 * encountered, -1 on error.
 */
static int
ensure_lock(kfs_context_t co, const char *path, off_t offset, size_t size,
        struct fuse_file_info *fi, struct flock *lock)
{
    uint64_t fh_backup = 0;
    int ret = 0;

    KFS_ENTER();

    lock->l_type = F_WRLCK;
    lock->l_whence = SEEK_SET; /* API requirement. */
    lock->l_start = offset;
    lock->l_len = size;
    fh_backup = fi->fh; /* mirror_lock() might spoil this */
    ret = mirror_lock(co, path, fi, F_SETLK, lock);
    fi->fh = fh_backup;
    switch (-ret) {
    case 0:
        /* Successfully acquired my own lock. */
        ret = 0;
    case EACCES:
    case EAGAIN:
        /* Some other process acquired the lock. Hope they called this func. */
        ret = 1;
        break;
    /* TODO: What about EDEADLK? */
    default:
        ret = -1;
        break;
    }

    KFS_RETURN(ret);
}

static int
mirror_getattr(const kfs_context_t co, const char *path, struct stat *stbuf)
{
    struct mirror_state * const state = co->priv;
    struct kfs_brick *subv = NULL;
    int ret = 0;

    KFS_ENTER();

    subv = get_one_reader(state, NULL);
    if (subv == NULL) {
        KFS_RETURN(-ENOSUBVOLS);
    }
    KFS_DO_OPER(ret = , subv, getattr, co, path, stbuf);

    KFS_RETURN(ret);
}

static int
mirror_readlink(const kfs_context_t co, const char *path, char *buf, size_t
        size)
{
    struct mirror_state * const state = co->priv;
    struct kfs_brick *subv = NULL;
    int ret = 0;

    KFS_ENTER();

    subv = get_one_reader(state, NULL);
    if (subv == NULL) {
        KFS_RETURN(-ENOSUBVOLS);
    }
    KFS_DO_OPER(ret = , subv, readlink, co, path, buf, size);

    KFS_RETURN(ret);
}

static int
mirror_mknod(const kfs_context_t co, const char *path, mode_t mode, dev_t dev)
{
    struct mirror_state * const state = co->priv;
    struct kfs_brick *subv = NULL;
    uint_t *ids = NULL;
    uint_t id = 0;
    uint_t i = 0;
    uint_t n = 0;
    int tmp = 0;
    int ret = 0;

    KFS_ENTER();

    ret = -ENOSUBVOLS;
    n = get_all_active_subvols(state, &ids);
    for (i = 0; i < n; i++) {
        id = ids[i];
        subv = C_get_subvol_by_ID(state, id);
        KFS_DO_OPER(ret = , subv, mknod, co, path, mode, dev);
        if (ret != 0) {
            /* An error occurred: try to rollback. */
            while (i--) {
                id = ids[i];
                subv = C_get_subvol_by_ID(state, id);
                KFS_DO_OPER(tmp = , subv, unlink, co, path);
                if (tmp != 0) {
                    KFS_ERROR("While trying to roll back a failed `new file' "
                        "operation by deleting it: could not delete `%s' from "
                        "node `%s': %s", path, subv->name, strerror(-tmp));
                    eject_subvolume(state, id);
                }
            }
            break;
        }
    }
    ids = KFS_FREE(ids);

    KFS_RETURN(ret);
}

/**
 * Truncate a file on all subvolumes to given length.
 *
 * The current implementation has no option for rollback: if one subvolume fails
 * to truncate it is immediately deactivated. Allowing rollback here could come
 * in two flavours:
 *
 * - Read the rest of the file into a buffer before truncating it. If any
 *   subvolume fails, try to restore the data on the other nodes with write().
 *   If that worked on all nodes, rollback was succesful! Big downside: every
 *   truncate() will cause a read() (which could be pretty big).
 *
 * - Copy the file on every node, and if all truncations succeeded delete the
 *   copies. This is only reasonable if all subvolumes implement copy-on-write,
 *   which is not exactly an assumption we can make here.
 *
 * Since both options have quite severe consequences, no rollback is possible
 * for now.
 */
static int
mirror_truncate(const kfs_context_t co, const char *path, off_t offset)
{
    struct mirror_state * const state = co->priv;
    struct kfs_brick *subv = NULL;
    uint_t *ids = NULL;
    uint_t id = 0;
    uint_t i = 0;
    uint_t n = 0;
    int ret = 0;

    KFS_ENTER();

    ret = -ENOSUBVOLS;
    n = get_all_active_subvols(state, &ids);
    for (i = 0; i < n; i++) {
        id = ids[i];
        subv = C_get_subvol_by_ID(state, id);
        KFS_DO_OPER(ret = , subv, truncate, co, path, offset);
        if (ret != 0) {
            if (i == 0) {
                /* Lucky: this was the first subvolume. Abort everything. */
                break;
            }
            KFS_ERROR("Could not truncate file %s on node %s: %s.", path,
                    subv->name, strerror(-ret));
            eject_subvolume(state, id);
            ret = 0;
            /* Try to stay consistent by truncating on other subvolumes too. */
            continue;
        }
    }
    ids = KFS_FREE(ids);

    KFS_RETURN(ret);
}

static int
mirror_open(const kfs_context_t co, const char *path, struct fuse_file_info *fi)
{
    struct mirror_state * const state = co->priv;
    struct kfs_brick *subv = NULL;
    struct mirror_fh *my_fh = NULL;
    uint_t accessmode = fi->flags & (O_RDONLY | O_WRONLY | O_RDWR);
    uint_t *ids = NULL;
    uint_t id = 0;
    uint_t i = 0;
    uint_t n = 0;
    int ret = 0;
    int tmp = 0;

    KFS_ENTER();

    switch (accessmode) {
    case O_RDONLY:
        /* Read-only requires just one subvolume. */
        my_fh = new_fh(1);
        if (my_fh == NULL) {
            KFS_RETURN(-ENOMEM);
        }
        subv = get_one_reader(state, &(my_fh->subvols_id[0]));
        if (subv == NULL) {
            /* No more readers available. */
            my_fh = del_fh(my_fh);
            KFS_RETURN(-ENOSUBVOLS);
        }
        KFS_DO_OPER(ret = , subv, open, co, path, fi);
        /* TODO: Try other subvolumes on failure. */
        if (ret != 0) {
            my_fh = del_fh(my_fh);
            KFS_RETURN(ret);
        }
        /* Store the filehandle returned by the subvolume. */
        my_fh->subvols_fh[0] = fi->fh;
        break;
    case O_RDWR:
    case O_WRONLY:
        /* All subvolumes must be available for modification. */
        n = get_all_active_subvols(state, &ids);
        if (n == 0) {
            KFS_RETURN(-ENOSUBVOLS);
        }
        my_fh = new_fh(n);
        /*
         * new_fh() also allocates space to store the ids, but we already have
         * them from get_all_active_subvols().
         */
        my_fh->subvols_id = KFS_FREE(my_fh->subvols_id);
        my_fh->subvols_id = ids;
        /* Call open() on all subvolumes and store their filehandles. */
        for (i = 0; i < n; i++) {
            id = my_fh->subvols_id[i];
            subv = C_get_subvol_by_ID(state, id);
            fi->fh = 0;
            KFS_DO_OPER(ret = , subv, open, co, path, fi);
            if (ret == 0) {
                my_fh->subvols_fh[i] = fi->fh;
            } else {
                /* An error occurred: try to rollback. */
                while (i--) {
                    id = my_fh->subvols_id[i];
                    fi->fh = my_fh->subvols_fh[i];
                    subv = C_get_subvol_by_ID(state, id);
                    KFS_DO_OPER(tmp = , subv, release, co, path, fi);
                    if (tmp != 0) {
                        KFS_ERROR("While trying to roll back a failed `open' "
                            "operation by closing it: could not close `%s' on "
                            "node `%s': %s", path, subv->name, strerror(-tmp));
                        eject_subvolume(state, id);
                    }
                }
                my_fh = del_fh(my_fh);
                KFS_RETURN(ret);
            }
        }
        break;
    default:
        KFS_ASSERT(0 && "Illegal flag.");
        break;
    }
    KFS_ASSERT(sizeof(my_fh) <= sizeof(fi->fh));
    /* Return the filehandle of this brick to the caller. */
    memcpy(&fi->fh, &my_fh, sizeof(my_fh));

    KFS_RETURN(0);
}

static int
mirror_mkdir(const kfs_context_t co, const char *path, mode_t mode)
{
    struct mirror_state * const state = co->priv;
    struct kfs_brick *subv = NULL;
    uint_t *ids = NULL;
    uint_t id = 0;
    uint_t i = 0;
    uint_t n = 0;
    int tmp = 0;
    int ret = 0;

    KFS_ENTER();

    ret = -ENOSUBVOLS;
    n = get_all_active_subvols(state, &ids);
    for (i = 0; i < n; i++) {
        id = ids[i];
        subv = C_get_subvol_by_ID(state, id);
        KFS_DO_OPER(ret = , subv, mkdir, co, path, mode);
        if (ret != 0) {
            /* An error occurred: try to rollback. */
            while (i--) {
                id = ids[i];
                subv = C_get_subvol_by_ID(state, id);
                KFS_DO_OPER(tmp = , subv, rmdir, co, path);
                if (tmp != 0) {
                    KFS_ERROR("While trying to roll back a failed mkdir "
                        "operation by deleting it: could not delete `%s' from "
                        "node `%s': %s", path, subv->name, strerror(-tmp));
                    eject_subvolume(state, id);
                }
            }
            break;
        }
    }
    ids = KFS_FREE(ids);

    KFS_RETURN(ret);
}
static int
mirror_unlink(const kfs_context_t co, const char *path)
{
    struct mirror_state * const state = co->priv;
    struct kfs_brick *subv = NULL;
    uint_t *ids = NULL;
    uint_t id = 0;
    uint_t i = 0;
    uint_t n = 0;
    int ret = 0;

    KFS_ENTER();

    ret = -ENOSUBVOLS;
    n = get_all_active_subvols(state, &ids);
    for (i = 0; i < n; i++) {
        id = ids[i];
        subv = C_get_subvol_by_ID(state, id);
        KFS_DO_OPER(ret = , subv, unlink, co, path);
        if (ret != 0) {
            if (i == 0) {
                /* Lucky: this was the first subvolume. Abort everything. */
                break;
            }
            KFS_ERROR("Could not delete file %s on node %s: %s.", path,
                    subv->name, strerror(-ret));
            eject_subvolume(state, id);
            ret = 0; /* The failed subvolume is gone, all others are OK. */
            /* Try to stay consistent by deleting on other subvolumes too. */
            continue;
        }
    }
    ids = KFS_FREE(ids);

    KFS_RETURN(ret);
}

static int
mirror_rmdir(const kfs_context_t co, const char *path)
{
    struct mirror_state *state = co->priv;
    struct kfs_brick *subv = NULL;
    uint_t *ids = NULL;
    uint_t id = 0;
    uint_t i = 0;
    uint_t n = 0;
    int ret = 0;

    KFS_ENTER();

    ret = -ENOSUBVOLS;
    n = get_all_active_subvols(state, &ids);
    for (i = 0; i < n; i++) {
        id = ids[i];
        subv = C_get_subvol_by_ID(state, id);
        KFS_DO_OPER(ret = , subv, rmdir, co, path);
        if (ret != 0) {
            if (i == 0) {
                /* Lucky: this was the first subvolume. Abort everything. */
                break;
            }
            KFS_ERROR("Could not delete directory %s on node %s: %s.", path,
                    subv->name, strerror(-ret));
            eject_subvolume(state, id);
            ret = 0;
            /* Try to stay consistent by deleting on other subvolumes too. */
            continue;
        }
    }
    ids = KFS_FREE(ids);

    KFS_RETURN(ret);
}

static int
mirror_symlink(const kfs_context_t co, const char *path1, const char *path2)
{
    struct mirror_state * const state = co->priv;
    struct kfs_brick *subv = NULL;
    uint_t *ids = NULL;
    uint_t id = 0;
    uint_t i = 0;
    uint_t n = 0;
    int ret = 0;
    int tmp = 0;

    KFS_ENTER();

    ret = -ENOSUBVOLS;
    n = get_all_active_subvols(state, &ids);
    for (i = 0; i < n; i++) {
        id = ids[i];
        subv = C_get_subvol_by_ID(state, id);
        KFS_DO_OPER(ret = , subv, symlink, co, path1, path2);
        if (ret != 0) {
            /* An error occurred: try to rollback. */
            while (i--) {
                id = ids[i];
                subv = C_get_subvol_by_ID(state, id);
                KFS_DO_OPER(tmp = , subv, unlink, co, path2);
                if (tmp != 0) {
                    KFS_ERROR("While trying to roll back a failed `new symlink'"
                        " operation by deleting it: could not delete `%s' from "
                        "node `%s': %s", path2, subv->name, strerror(-tmp));
                    eject_subvolume(state, id);
                }
            }
            break;
        }
    }
    ids = KFS_FREE(ids);

    KFS_RETURN(ret);
}

static int
mirror_rename(const kfs_context_t co, const char *from, const char *to)
{
    struct mirror_state * const state = co->priv;
    struct kfs_brick *subv = NULL;
    uint_t *ids = NULL;
    uint_t id = 0;
    uint_t i = 0;
    uint_t n = 0;
    int ret = 0;
    int tmp = 0;

    KFS_ENTER();

    ret = -ENOSUBVOLS;
    n = get_all_active_subvols(state, &ids);
    for (i = 0; i < n; i++) {
        id = ids[i];
        subv = C_get_subvol_by_ID(state, id);
        KFS_DO_OPER(ret = , subv, rename, co, from, to);
        if (ret != 0) {
            /* An error occurred: try to rollback. */
            while (i--) {
                id = ids[i];
                subv = C_get_subvol_by_ID(state, id);
                KFS_DO_OPER(tmp = , subv, rename, co, to, from);
                if (tmp != 0) {
                    KFS_ERROR("While trying to roll back a failed `rename' "
                        "operation: renaming back from `%s' to `%s' on node "
                        "`%s' failed: %s", to, from, subv->name,
                        strerror(-tmp));
                    eject_subvolume(state, id);
                }
            }
            break;
        }
    }
    ids = KFS_FREE(ids);

    KFS_RETURN(ret);
}

static int
mirror_link(const kfs_context_t co, const char *from, const char *to)
{
    struct mirror_state * const state = co->priv;
    struct kfs_brick *subv = NULL;
    uint_t *ids = NULL;
    uint_t id = 0;
    uint_t i = 0;
    uint_t n = 0;
    int ret = 0;
    int tmp = 0;

    KFS_ENTER();

    ret = -ENOSUBVOLS;
    n = get_all_active_subvols(state, &ids);
    for (i = 0; i < n; i++) {
        id = ids[i];
        subv = C_get_subvol_by_ID(state, id);
        KFS_DO_OPER(ret = , subv, link, co, from, to);
        if (ret != 0) {
            /* An error occurred: try to rollback. */
            while (i--) {
                id = ids[i];
                subv = C_get_subvol_by_ID(state, id);
                KFS_DO_OPER(tmp = , subv, unlink, co, to);
                if (tmp != 0) {
                    KFS_ERROR("While trying to roll back a failed hardlink "
                        "operation by deleting it: could not delete `%s' from "
                        "node `%s': %s", to, subv->name, strerror(-tmp));
                    eject_subvolume(state, id);
                }
            }
            break;
        }
    }
    ids = KFS_FREE(ids);

    KFS_RETURN(ret);
}

static int
mirror_chmod(const kfs_context_t co, const char *path, mode_t mode)
{
    struct mirror_state * const state = co->priv;
    struct kfs_brick *subv = NULL;
    /** Backup of the mode in case of rollback (if possible). */
    struct {
        struct stat stbuf;
        mode_t mode;
        uint_t valid;
    } backup = {.mode = 0, .valid = 0};
    uint_t *ids = NULL;
    uint_t id = 0;
    uint_t i = 0;
    uint_t n = 0;
    uint_t one_success = 0;
    int ret = 0;
    int tmp = 0;

    KFS_ENTER();

    /* Backup old mode. */
    ret = mirror_getattr(co, path, &backup.stbuf);
    if (ret == 0) {
        backup.mode = backup.stbuf.st_mode & PERM7777;
        backup.valid = 1;
    } else {
        backup.valid = 0;
    }
    /* Perform mode change on all subvols. */
    ret = -ENOSUBVOLS;
    n = get_all_active_subvols(state, &ids);
    for (i = 0; i < n; i++) {
        id = ids[i];
        subv = C_get_subvol_by_ID(state, id);
        KFS_DO_OPER(ret = , subv, chmod, co, path, mode);
        if (ret == 0) {
            one_success = 1;
        } else if (one_success && backup.valid == 0) {
            /* No possibility to rollback: eject this node and continue. */
            KFS_ERROR("Changing mode of `%s' failed midway at node `%s': "
                "%s. Was unable to backup old mode; rollback impossible, "
                "continuing with operation.",
                path, subv->name, strerror(-errno));
            eject_subvolume(state, id);
            ret = 0;
        } else {
            /* Rollback succesful subvolumes and abort operation. */
            while (i--) {
                id = ids[i];
                subv = C_get_subvol_by_ID(state, id);
                KFS_DO_OPER(tmp = , subv, chmod, co, path, backup.mode);
                if (tmp != 0) {
                    KFS_ERROR("While trying to roll back a failed `chmod' "
                        "operation by reverting it: could not chmod `%s' from "
                        "node `%s': %s", path, subv->name, strerror(-tmp));
                    eject_subvolume(state, id);
                }
            }
            break;
        }
    }
    ids = KFS_FREE(ids);

    KFS_RETURN(ret);
}

static int
mirror_chown(const kfs_context_t co, const char *path, uid_t uid, gid_t gid)
{
    struct mirror_state * const state = co->priv;
    struct kfs_brick *subv = NULL;
    struct stat stbuf;
    /** Backup in case of rollback. */
    struct {
        uid_t uid;
        gid_t gid;
        uint_t valid;
    } backup = {.uid = 0, .gid = 0, .valid = 0};
    uint_t *ids = NULL;
    uint_t id = 0;
    uint_t i = 0;
    uint_t n = 0;
    uint_t one_success = 0;
    int ret = 0;
    int tmp = 0;

    KFS_ENTER();

    /* Backup old permissions. */
    ret = mirror_getattr(co, path, &stbuf);
    if (ret == 0) {
        backup.uid = stbuf.st_uid;
        backup.gid = stbuf.st_gid;
        backup.valid = 1;
    } else {
        backup.valid = 0;
    }
    /* Perform mode change on all subvols. */
    ret = -ENOSUBVOLS;
    one_success = 0;
    n = get_all_active_subvols(state, &ids);
    for (i = 0; i < n; i++) {
        id = ids[i];
        subv = C_get_subvol_by_ID(state, id);
        KFS_DO_OPER(ret = , subv, chown, co, path, uid, gid);
        if (ret == 0) {
            one_success = 1;
        } else if (one_success && backup.valid == 0) {
            /* No possibility to rollback: eject this node and continue. */
            KFS_ERROR("Changing ownership of `%s' failed midway at node "
                "`%s': %s. Was unable to backup old ownership; rollback "
                "impossible, continuing with operation.",
                path, subv->name, strerror(-errno));
            eject_subvolume(state, id);
            ret = 0;
        } else {
            /* Rollback succesful subvolumes and abort operation. */
            while (i--) {
                id = ids[i];
                subv = C_get_subvol_by_ID(state, id);
                KFS_DO_OPER(tmp = , subv, chown, co, path, backup.uid,
                        backup.gid);
                if (tmp != 0) {
                    KFS_ERROR("While trying to roll back a failed `chown' "
                        "operation by reverting it: could not chown `%s' from "
                        "node `%s': %s", path, subv->name, strerror(-tmp));
                    eject_subvolume(state, id);
                }
            }
            break;
        }
    }
    ids = KFS_FREE(ids);

    KFS_RETURN(ret);
}

static int
mirror_read(const kfs_context_t co, const char *path, char *buf, size_t size,
        off_t offset, struct fuse_file_info *fi)
{
    struct mirror_state * const state = co->priv;
    struct mirror_fh *my_fh = NULL;
    struct kfs_brick *subv = NULL;
    uint_t id = 0;
    uint_t i = 0;
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(sizeof(my_fh) <= sizeof(fi->fh));
    memcpy(&my_fh, &fi->fh, sizeof(my_fh));
    i = 0;
    do {
        if (i == my_fh->num_subvols) {
            KFS_RETURN(-ENOSUBVOLS);
        }
        id = my_fh->subvols_id[i];
    } while (is_active(state, id) == 0);
    subv = C_get_subvol_by_ID(state, id);
    fi->fh = my_fh->subvols_fh[i];
    KFS_DO_OPER(ret = , subv, read, co, path, buf, size, offset, fi);

    KFS_RETURN(ret);
}

/**
 * Write data to (part of) a file.
 *
 * Before performing the write, the part of the file that would be overwritten
 * is read into a backup buffer. If this operation fails on some subvolume, the
 * subvolumes that were succesfully updated are overwritten once more with the
 * old data and the handler returns with the error condition from the failing
 * brick.
 *
 * Regarding backups, consider writes on the same part of a file, with
 * subvolumes A and B:
 *
 * 1) Process P1 calls write().
 *
 * 2) This handler backs up the old data.
 *
 * 3) A is updated.
 *
 * 4) Process P2 calls write() (or it moves / deletes / etc the file).
 *
 * 5) Control goes to the thread handling P2's write and the file is updated on
 *    A and B. Write returns to P2 indicating success.
 *
 * 6) Control goes back to the thread handling P1's write.
 *
 * 7) Updating B fails, thus it retains the version as written by P2.
 *
 * 8) This handler rolls the file on A back to its version before P2.
 *
 * 9) It returns to P1 indicating an error.
 *
 * 10) The bricks are out of sync but both are active.
 *
 * To prevent this, a lock is acquired before backing up. If the file is already
 * locked, from the fact that this handler is even running at all it is
 * concluded that it must be the calling process that has the lock, which is
 * considered just as good.
 *
 * TODO: Actually, it is not: what if it is an evil process that forks and then
 * acts like both P1 and P2? It could DOS this brick. I have no idea how to deal
 * with that without removing the backup functionality altogether.
 */
static int
mirror_write(const kfs_context_t co, const char *path, const char *buf, size_t
        size, off_t offset, struct fuse_file_info *fi)
{
    struct mirror_state * const state = co->priv;
    struct mirror_fh *my_fh = NULL;
    struct kfs_brick *subv = NULL;
    struct {
        struct flock lock;
        char *buf;
        uint_t mylock : 1; /* I acquired this lock! */
        uint_t valid : 1;
    } backup = {.buf = NULL, .mylock = 0, .valid = 0};
    uint_t one_success = 0;
    uint_t id = 0;
    uint_t i = 0;
    int ret = 0;
    int tmp = 0;

    KFS_ENTER();

    /* Backup the data first. */
    backup.valid = 0;
    backup.buf = KFS_MALLOC(size);
    if (backup.buf != NULL) {
        /* Acquire the lock. */
        ret = ensure_lock(co, path, offset, size, fi, &backup.lock);
        switch (ret) {
        case -1:
            backup.valid = 0;
            break;
        case 0:
            backup.valid = 1;
            backup.mylock = 1;
            break;
        case 1:
            backup.valid = 1;
            backup.mylock = 0;
            break;
        default:
            KFS_ASSERT(0 && "Illegal return value from ensure_lock()!");
            break;
        }
    }
    /* Perform the actual backup. */
    if (backup.valid) {
        ret = mirror_read(co, path, backup.buf, size, offset, fi);
        if (ret != size) {
            backup.valid = 0;
        }
    }
    /* Write new data. */
    KFS_ASSERT(sizeof(my_fh) <= sizeof(fi->fh));
    memcpy(&my_fh, &fi->fh, sizeof(my_fh));
    ret = -ENOSUBVOLS;
    one_success = 0;
    for (i = 0; i < my_fh->num_subvols; i++) {
        id = my_fh->subvols_id[i];
        if (is_active(state, id) == 0) {
            continue;
        }
        subv = C_get_subvol_by_ID(state, id);
        fi->fh = my_fh->subvols_fh[i];
        KFS_DO_OPER(ret = , subv, write, co, path, buf, size, offset, fi);
        KFS_ASSERT(ret == size || ret < 0);
        if (ret == size) {
            one_success = 1;
        } else if (one_success && backup.valid == 0) {
            KFS_ERROR("Writing new data to `%s' failed midway at node `%s': %s;"
                " rollback impossible, continuing with operation.", path,
                subv->name, strerror(-errno));
            eject_subvolume(state, id);
            ret = 0;
        } else {
            /* Rollback succesful subvolumes and abort operation. */
            while (i--) {
                id = my_fh->subvols_id[i];
                if (is_active(state, id) == 0) {
                    continue;
                }
                fi->fh = my_fh->subvols_fh[i];
                subv = C_get_subvol_by_ID(state, id);
                KFS_DO_OPER(tmp = , subv, write, co, path, backup.buf, size,
                        offset, fi);
                if (tmp != size) {
                    /* API requirement. */
                    KFS_ASSERT(tmp < 0);
                    KFS_ERROR("While trying to roll back a failed write "
                        "operation by reverting it: could not write to %s on "
                        "node `%s': %s", path, subv->name, strerror(-ret));
                    eject_subvolume(state, id);
                }
            }
            break;
        }
    }
    if (backup.buf != NULL) {
        backup.buf = KFS_FREE(backup.buf);
    }
    /* Release the lock, if acquired. */
    if (backup.mylock) {
        backup.lock.l_type = F_UNLCK;
        tmp = mirror_lock(co, path, fi, F_SETLK, &backup.lock);
        if (tmp != 0) {
            KFS_ERROR("Acquired a temp lock on `%s' but now I can not unlock "
                      "it! Error: %s", path, strerror(-tmp));
            /* TODO: What else can be done at this point? */
        }
    }

    KFS_RETURN(ret);
}

static int
mirror_statfs(const kfs_context_t co, const char *path, struct statvfs *stbuf)
{
    struct mirror_state * const state = co->priv;
    struct kfs_brick *subv = NULL;
    int ret = 0;

    KFS_ENTER();

    subv = get_one_reader(state, NULL);
    if (subv == NULL) {
        KFS_RETURN(-ENOSUBVOLS);
    }
    KFS_DO_OPER(ret = , subv, statfs, co, path, stbuf);

    KFS_RETURN(ret);
}

/**
 * Flush user-space buffers for this path on all subvolumes in this session.
 *
 * This is a relatively fragile operation: errors on deferred writes will show
 * up here instead of at write(), and rolling back properly at this point is
 * quite a different ballgame. No rollback is implemented at all: any node that
 * fails here will be deactivated (unless the first one fails right away), while
 * the error-probability is actually relatively high on this operation.
 *
 * In other words: this is a serious weak spot. There are some ways to reconcile
 * robustness, but they have their price. The most obvious solution that I can
 * think of for now is making this brick perform a flush after every operation
 * that changes the state of a subvolume, effectively disabling all user-space
 * caching to catch errors where they take place and allow for some reasonable
 * rollback mechanisms. The drawbacks are obvious.
 *
 * TODO: Fix this problem.
 */
static int
mirror_flush(const kfs_context_t co, const char *path, struct fuse_file_info
        *fi)
{
    struct mirror_state * const state = co->priv;
    struct mirror_fh *my_fh = NULL;
    struct kfs_brick *subv = NULL;
    uint_t one_success = 0;
    uint_t id = 0;
    uint_t i = 0;
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(sizeof(my_fh) <= sizeof(fi->fh));
    memcpy(&my_fh, &fi->fh, sizeof(my_fh));
    KFS_ASSERT(my_fh != NULL);
    ret = -ENOSUBVOLS;
    one_success = 0;
    for (i = 0; i < my_fh->num_subvols; i++) {
        id = my_fh->subvols_id[i];
        if (is_active(state, id) == 0) {
            continue;
        }
        subv = C_get_subvol_by_ID(state, id);
        fi->fh = my_fh->subvols_fh[i];
        KFS_DO_OPER(ret = , subv, flush, co, path, fi);
        if (ret == 0) {
            one_success = 1;
        } else if (one_success == 0) {
            break;
        } else {
            KFS_ERROR("Flushing `%s' failed on node `%s': %s. Already flushed "
                "some other nodes; dropping this one and continuing with the "
                "rest.", path, subv->name, strerror(-ret));
            eject_subvolume(state, id);
            ret = 0;
        }
    }

    KFS_RETURN(ret);
}

/**
 * Close this session on all subvolumes associated with it.
 *
 * While most operation handlers try to avoid acting on inactive subvolumes,
 * this one explicitly frees the resource on all of them. It just ignores the
 * return value from inactive ones. The rationale is that, even if a subvolume
 * was deactivated by this brick, it might not have free'd the resources for
 * this sessions internally. This gives them at least an opportunity to do that.
 *
 * Active subvolumes returning an error are, however, not ignored: they are
 * deactivated. TODO: It might be nicer to see if maybe the bricks that
 * succesfully closed a file could reopen it. If that would not violate POSIX
 * constraints, it might be considered a rollback. This would allow passing the
 * error back to the caller instead of ejecting the subvolume.
 */
static int
mirror_release(const kfs_context_t co, const char *path, struct fuse_file_info
        *fi)
{
    struct mirror_state * const state = co->priv;
    struct mirror_fh *my_fh = NULL;
    struct kfs_brick *subv = NULL;
    uint_t id = 0;
    uint_t i = 0;
    uint_t one_success = 0;
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(sizeof(my_fh) <= sizeof(fi->fh));
    memcpy(&my_fh, &fi->fh, sizeof(my_fh));
    KFS_ASSERT(my_fh != NULL);
    ret = -ENOSUBVOLS;
    one_success = 0;
    for (i = 0; i < my_fh->num_subvols; i++) {
        id = my_fh->subvols_id[i];
        fi->fh = my_fh->subvols_fh[i];
        subv = C_get_subvol_by_ID(state, id);
        KFS_DO_OPER(ret = , subv, release, co, path, fi);
        if (ret == 0) {
            one_success = 1;
        } else if (is_active(state, id)) {
            /* If the first active node failed, forget about the whole thing. */
            if (one_success == 0) {
                KFS_RETURN(ret);
            }
            KFS_ERROR("Closing file `%s' on node `%s' failed: %s, dropping "
                    "node.", path, subv->name, strerror(-ret));
            eject_subvolume(state, id);
        }
    }
    my_fh = del_fh(my_fh);

    KFS_RETURN(ret);
}

static int
mirror_fsync(const kfs_context_t co, const char *path, int isdatasync, struct
        fuse_file_info *fi)
{
    struct mirror_state * const state = co->priv;
    struct mirror_fh *my_fh = NULL;
    struct kfs_brick *subv = NULL;
    uint_t one_success = 0;
    uint_t id = 0;
    uint_t i = 0;
    int ret = 0;

    KFS_ENTER();

    KFS_ASSERT(sizeof(my_fh) <= sizeof(fi->fh));
    memcpy(&my_fh, &fi->fh, sizeof(my_fh));
    KFS_ASSERT(my_fh != NULL);
    ret = -ENOSUBVOLS;
    one_success = 0;
    for (i = 0; i < my_fh->num_subvols; i++) {
        id = my_fh->subvols_id[i];
        if (is_active(state, id) == 0) {
            continue;
        }
        fi->fh = my_fh->subvols_fh[i];
        subv = C_get_subvol_by_ID(state, id);
        KFS_DO_OPER(ret = , subv, fsync, co, path, isdatasync, fi);
        if (ret == 0) {
            one_success = 1;
        } else if (one_success == 0) {
            break;
        } else {
            KFS_ERROR("Node `%s' failed to synchronise file `%s' to storage, "
                      "deactivating it.", subv->name, path);
            eject_subvolume(state, id);
            ret = 0;
        }
    }

    KFS_RETURN(ret);
}

static int
mirror_getxattr(const kfs_context_t co, const char *path, const char *name, char
        *value, size_t size)
{
    struct mirror_state * const state = co->priv;
    struct kfs_brick *subv = NULL;
    int ret = 0;

    KFS_ENTER();

    subv = get_one_reader(state, NULL);
    if (subv == NULL) {
        KFS_RETURN(-ENOSUBVOLS);
    }
    KFS_DO_OPER(ret = , subv, getxattr, co, path, name, value, size);

    KFS_RETURN(ret);
}

/**
 * Set an extended attribute on this file.
 *
 * The error handling for this code is so hideous that it would be
 * disproportional, if not for the fact that extended attributes, unlike a lot
 * of other operations, actually fail most of the time (because a lot of
 * filesystems do not support them).
 *
 * The issue (and an inherent shortcoming of the API) is that the caller does
 * not need to open a file in order to change an attribute on it. This, of
 * course, is race-condition sensitivity galore on its own already. However,
 * trying not to fuel the fire, this operation acts just like mirror_write()
 * regarding backups: lock a file, make a backup, restore that backup on
 * failure, release the lock. Now locking (mirror_lock()) /does/ require the
 * file to be open. Result: this handler must open the file.
 */
static int
mirror_setxattr(const kfs_context_t co, const char *path, const char *name,
        const char *value, size_t size, int flags)
{
    struct mirror_state * const state = co->priv;
    struct kfs_brick *subv = NULL;
    struct {
        struct fuse_file_info fi;
        struct flock lock;
        char *buf;
        size_t size;
        uint_t mylock : 1;
        uint_t opened : 1; /* File was opened succesfully. */
        uint_t valid : 1;
    } backup = {.buf = NULL, .size = 0, .mylock = 0, .opened = 0, .valid = 0};
    uint_t *ids = NULL;
    uint_t id = 0;
    uint_t i = 0;
    uint_t n = 0;
    uint_t one_success = 0;
    int ret = 0;
    int tmp = 0;
    /* Oh the humanity.. */

    KFS_ENTER();

    /* Backup the old attribute (if any). */
    /* Open the file (for locking). */
    memset(&backup.fi, 0, sizeof(backup.fi));
    backup.fi.flags = O_RDONLY;
    ret = mirror_open(co, path, &backup.fi);
    backup.valid = 0;
    backup.opened = 0;
    if (ret == 0) {
        backup.opened = 1;
        /* Acquire the lock. */
        ret = ensure_lock(co, path, 0, 0, &backup.fi, &backup.lock);
        switch (ret) {
        case -1:
            backup.valid = 0;
            break;
        case 0:
            backup.valid = 1;
            backup.mylock = 1;
            break;
        case 1:
            backup.valid = 1;
            backup.mylock = 0;
            break;
        default:
            KFS_ASSERT(0 && "Illegal return value from ensure_lock()!");
            break;
        }
    }
    /* Now try backing up the data. */
    if (backup.valid) {
        ret = mirror_getxattr(co, path, name, NULL, 0);
        if (ret < 0) {
            backup.valid = 0;
        } else {
            backup.size = ret;
            backup.buf = KFS_MALLOC(backup.size);
            if (backup.buf == NULL) {
                backup.valid = 0;
            } else if (backup.size != 0) {
                /* Don't bother doing this if it is empty anyway. */
                ret = mirror_getxattr(co, path, name, backup.buf, backup.size);
                if (ret < 0) {
                    backup.valid = 0;
                } else if (ret != backup.size) {
                    /*
                     * The only way the size could have changed is if the
                     * process owning this lock invoked a race-condition and
                     * changed it since the previous getxattr call that checks
                     * the size. While not devastating here, it could do the
                     * same later during rollback to DOS this brick, see
                     * mirror_write(). TODO: What to do?
                     */
                    KFS_ABORT("DOS-like process behaviour detected.");
                }
            }
        }
    }
    /* (Hopefully) done backing up, now update the actual attribute. */
    ret = -ENOSUBVOLS;
    n = get_all_active_subvols(state, &ids);
    one_success = 0;
    for (i = 0; i < n; i++) {
        id = ids[i];
        subv = C_get_subvol_by_ID(state, id);
        KFS_DO_OPER(ret = , subv, setxattr, co, path, name, value, size, flags);
        if (ret == 0) {
            one_success = 1;
        } else if (one_success && backup.valid == 0) {
            KFS_ERROR("Set extended attribute `%s' for `%s' on node `%s' failed"
                      ", could not rollback: dropping node.", name, path,
                      subv->name);
            eject_subvolume(state, id);
        } else {
            /* Restore the backup on all subvolumes that did succeed. */
            while (i--) {
                id = ids[i];
                subv = C_get_subvol_by_ID(state, id);
                KFS_DO_OPER(tmp = , subv, setxattr, co, path, name, backup.buf,
                        backup.size, XATTR_REPLACE);
                if (tmp != 0) {
                    KFS_ERROR("Could not rollback extended attribute `%s' for "
                              "`%s' on node `%s' after error: %s. Deactivate.",
                              name, path, subv->name, strerror(-tmp));
                }
            }
            break;
        }
    }
    /* Release all temporary resources. */
    if (backup.mylock) {
        backup.lock.l_type = F_UNLCK;
        tmp = mirror_lock(co, path, &backup.fi, F_SETLK, &backup.lock);
        if (tmp != 0) {
            KFS_ERROR("Acquired a temp lock on `%s' but now I can not unlock "
                      "it! Error: %s", path, strerror(-tmp));
            /* TODO: What else can be done at this point? */
        }
    }
    if (backup.opened) {
        tmp = mirror_release(co, path, &backup.fi);
        if (tmp != 0) {
            KFS_ERROR("Opened `%s' temporarily but now I can not close it!",
                    path);
            /* TODO: What else can be done at this point? */
        }
    }
    if (backup.buf != NULL) {
        backup.buf = KFS_FREE(backup.buf);
    }
    ids = KFS_FREE(ids);

    KFS_RETURN(ret);
}

/**
 * Open a directory session.
 *
 * These are only used for readdir so only one subvolume is opened. TODO:
 * Opendir on multiple subvolumes for fallback in case readdir fails for the
 * first one.
 */
static int
mirror_opendir(const kfs_context_t co, const char *path, struct fuse_file_info
        *fi)
{
    struct mirror_state * const state = co->priv;
    struct mirror_dirfh *my_dirfh = NULL;
    struct kfs_brick *subv = NULL;
    int ret = 0;

    KFS_ENTER();

    /* Read-only requires just one subvolume. */
    my_dirfh = KFS_MALLOC(sizeof(*my_dirfh));
    if (my_dirfh == NULL) {
        KFS_RETURN(-ENOMEM);
    }
    subv = get_one_reader(state, NULL);
    if (subv == NULL) {
        my_dirfh = KFS_FREE(my_dirfh);
        KFS_RETURN(-ENOSUBVOLS);
    }
    KFS_DO_OPER(ret = , subv, opendir, co, path, fi);
    if (ret != 0) {
        my_dirfh = KFS_FREE(my_dirfh);
    } else {
        /* Store the filehandle returned by the subvolume. */
        my_dirfh->fh = fi->fh;
        my_dirfh->subv = subv;
        KFS_ASSERT(sizeof(my_dirfh) <= sizeof(fi->fh));
        /* Return the filehandle of this brick to the caller. */
        memcpy(&fi->fh, &my_dirfh, sizeof(my_dirfh));
    }

    KFS_RETURN(ret);
}

static int
mirror_readdir(const kfs_context_t co, const char *path, void *buf,
        fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    struct mirror_dirfh *my_dirfh = NULL;
    struct kfs_brick *subv = NULL;
    int ret = 0;

    KFS_ENTER();

    memcpy(&my_dirfh, &fi->fh, sizeof(my_dirfh));
    KFS_ASSERT(my_dirfh != NULL);
    subv = my_dirfh->subv;
    fi->fh = my_dirfh->fh;
    KFS_DO_OPER(ret = , subv, readdir, co, path, buf, filler, offset, fi);

    KFS_RETURN(ret);
}

static int
mirror_releasedir(const kfs_context_t co, const char *path, struct
        fuse_file_info *fi)
{
    struct mirror_dirfh *my_dirfh = NULL;
    struct kfs_brick *subv = NULL;
    int ret = 0;

    KFS_ENTER();

    memcpy(&my_dirfh, &fi->fh, sizeof(my_dirfh));
    KFS_ASSERT(my_dirfh != NULL);
    subv = my_dirfh->subv;
    fi->fh = my_dirfh->fh;
    KFS_DO_OPER(ret = , subv, releasedir, co, path, fi);
    /* Only delete the resources if closing was succesful. */
    if (ret == 0) {
        my_dirfh = KFS_FREE(my_dirfh);
    }

    KFS_RETURN(ret);
}

/**
 * Update access and modification time.
 *
 * If some brick fails to do this, the others where it did work are rolled back
 * by restoring the modification time. The access time, however, will be
 * not be restored to its original (because the API does not allow this).
 *
 * Also notice that precision could be lost in the backup process: while the API
 * allows setting nanosecond precision, it only allows fetching with any
 * precision that the `time_t' datatype allows.
 */
static int
mirror_utimens(const kfs_context_t co, const char *path, const struct timespec
        tvnano[2])
{
    struct mirror_state * const state = co->priv;
    struct kfs_brick *subv = NULL;
    struct {
        struct stat stbuf;
        time_t mtime;
        uint_t valid;
        struct timespec tvnano[2];
    } backup = {.mtime = 0, .valid = 0};
    uint_t *ids = NULL;
    uint_t id = 0;
    uint_t i = 0;
    uint_t n = 0;
    uint_t one_success = 0;
    int ret = 0;
    int tmp = 0;

    KFS_ENTER();

    /* Backup mtime. */
    ret = mirror_getattr(co, path, &backup.stbuf);
    if (ret == 0) {
        backup.mtime = backup.stbuf.st_mtime;
        backup.valid = 1;
    } else {
        backup.valid = 0;
    }
    /* Perform time change on all subvols. */
    ret = -ENOSUBVOLS;
    n = get_all_active_subvols(state, &ids);
    for (i = 0; i < n; i++) {
        id = ids[i];
        subv = C_get_subvol_by_ID(state, id);
        KFS_DO_OPER(ret = , subv, utimens, co, path, tvnano);
        if (ret == 0) {
            one_success = 1;
        } else if (one_success && backup.valid == 0) {
            /* No possibility to rollback: eject this node and continue. */
            KFS_ERROR("Changing a/mtime of `%s' failed midway at node `%s': "
                "%s. Was unable to backup old mtime; rollback impossible, "
                "continuing with operation.",
                path, subv->name, strerror(-errno));
            eject_subvolume(state, id);
            ret = 0;
        } else {
            /* Rollback succesful subvolumes and abort operation. */
            backup.tvnano[0].tv_sec = backup.mtime;
            /* Hope it's typedef float. */
            backup.tvnano[0].tv_nsec = backup.mtime % 1;
            backup.tvnano[1].tv_sec = backup.mtime;
            backup.tvnano[1].tv_nsec = backup.mtime % 1;
            while (i--) {
                id = ids[i];
                subv = C_get_subvol_by_ID(state, id);
                KFS_DO_OPER(tmp = , subv, utimens, co, path, backup.tvnano);
                if (tmp != 0) {
                    KFS_ERROR("While trying to roll back a failed `utimens' "
                        "operation by reverting it: failed on `%s' from "
                        "node `%s': %s", path, subv->name, strerror(-tmp));
                    eject_subvolume(state, id);
                }
            }
            break;
        }
    }
    ids = KFS_FREE(ids);

    KFS_RETURN(ret);
}

static const struct kfs_operations handlers = {
    .getattr = mirror_getattr,
    .readlink = mirror_readlink,
    .mknod = mirror_mknod,
    .mkdir = mirror_mkdir,
    .unlink = mirror_unlink,
    .rmdir = mirror_rmdir,
    .symlink = mirror_symlink,
    .rename = mirror_rename,
    .link = mirror_link,
    .chmod = mirror_chmod,
    .chown = mirror_chown,
    .truncate = mirror_truncate,
    .open = mirror_open,
    .read = mirror_read,
    .write = mirror_write,
    .statfs = mirror_statfs,
    .flush = mirror_flush,
    .release = mirror_release,
    .fsync = mirror_fsync,
    .setxattr = mirror_setxattr,
    .getxattr = mirror_getxattr,
    .listxattr = nosys_listxattr,
    .removexattr = nosys_removexattr,
    .opendir = mirror_opendir,
    .readdir = mirror_readdir,
    .releasedir = mirror_releasedir,
    .fsyncdir = nosys_fsyncdir,
    .access = nosys_access,
    .create = nosys_create,
    .ftruncate = nosys_ftruncate,
    .fgetattr = nosys_fgetattr,
    .lock = mirror_lock,
    .utimens = mirror_utimens,
    .bmap = nosys_bmap,
#if FUSE_VERSION >= 28
    .ioctl = nosys_ioctl,
    .poll = nosys_poll,
#endif
};

/**
 * Create new freshly allocated state for this brick.
 */
static struct mirror_state *
new_state(const struct kfs_brick subvols[], uint_t n)
{
    struct mirror_state *s = NULL;
    size_t size = 0;
    uint_t i = 0;
    int ret = 0;

    KFS_ENTER();

    size = sizeof(*s);
    s = KFS_MALLOC(size);
    if (s != NULL) {
        size = sizeof(*s->C.subvols) * n;
        s->C.subvols = KFS_MALLOC(size);
        if (s->C.subvols != NULL) {
            s->C.subvols = memcpy(s->C.subvols, subvols, size);
            size = sizeof(*s->L.subvol_active) * n;
            s->L.subvol_active = KFS_MALLOC(size);
            if (s->L.subvol_active != NULL) {
                for (i = 0; i < n; i++) {
                    s->L.subvol_active[i] = 1;
                }
                ret = kfs_rwlock_init(&s->lock);
                if (ret == 0) {
                    s->C.num_subvols = n;
                    KFS_RETURN(s);
                }
                s->L.subvol_active = KFS_FREE(s->L.subvol_active);
            }
            s->C.subvols = KFS_FREE(s->C.subvols);
        }
        s = KFS_FREE(s);
    }

    KFS_RETURN(NULL);
}

/**
 * Clean up a mirror_state structure.
 */
static struct mirror_state *
del_state(struct mirror_state *s)
{
    KFS_ENTER();

    KFS_ASSERT(s->L.subvol_active != NULL);
    s->L.subvol_active = KFS_FREE(s->L.subvol_active);
    KFS_ASSERT(s->C.subvols != NULL);
    s->C.subvols = KFS_FREE(s->C.subvols);
    KFS_ASSERT(s != NULL);
    s = KFS_FREE(s);

    KFS_RETURN(s);
}
    

/**
 * Global initialization. Requires exactly two subvolumes: the first one is the
 * origin, the second one is the cache.
 */
static void *
kfs_mirror_init(const char *conffile, const char *section, uint_t
        num_subvolumes, const struct kfs_brick subvolumes[])
{
    KFS_NASSERT((void) conffile);
    (void) section;

    struct mirror_state *s = NULL;

    KFS_ENTER();

    KFS_ASSERT(conffile != NULL && section != NULL && subvolumes != NULL);
    if (num_subvolumes == 0) {
        KFS_ERROR("At least one subvolume required by brick %s.", section);
        KFS_RETURN(NULL);
    }
    s = new_state(subvolumes, num_subvolumes);

    KFS_RETURN(s);
}

/*
 * Get the backend interface.
 */
static const struct kfs_operations *
kfs_mirror_getfuncs(void)
{
    KFS_ENTER();

    KFS_RETURN(&handlers);
}

/**
 * Global cleanup.
 *
 * Results are undefined if this is called while other operation handlers are
 * still in progress.
 */
static void
kfs_mirror_halt(void *private_data)
{
    struct mirror_state *state = private_data;

    KFS_ENTER();

    kfs_rwlock_destroy(&state->lock);
    state = del_state(state);

    KFS_RETURN();
}

static const struct kfs_brick_api kfs_mirror_api = {
    .init = kfs_mirror_init,
    .getfuncs = kfs_mirror_getfuncs,
    .halt = kfs_mirror_halt,
};

const struct kfs_brick_api *
kfs_brick_getapi(void)
{
    KFS_ENTER();

    KFS_RETURN(&kfs_mirror_api);
}
