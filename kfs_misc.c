/**
 * Some utility functions.
 */

#include <arpa/inet.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/stat.h>
/* For sleep(). */
#include <unistd.h>

#include "minini/minini.h"

#include "kfs_misc.h"

#include "kfs.h"
#include "kfs_memory.h"

inline int
min(int x, int y)
{
    return x > y ? y : x;
}

inline int
max(int x, int y)
{
    return x < y ? y : x;
}

inline int
xor(int x, int y)
{
    return !x ^ !y;
}

/**
 * Sleep for the given number of seconds.
 */
inline unsigned int
kfs_sleep(unsigned int seconds)
{
    return sleep(seconds);
}

/**
 * Concatenate two '\0'-delimited character arrays. Returns a freshly allocated
 * array which must eventually be freed or NULL on failure.
 */
char *
kfs_strcat(const char *part1, const char *part2)
{
    /* String lengths *without* appending null-byte. */
    size_t len1 = 0;
    size_t len2 = 0;
    size_t len_result = 0;
    char *result = NULL;

    KFS_ENTER();

    KFS_ASSERT(part1 != NULL && part2 != NULL);
    len1 = strlen(part1);
    len2 = strlen(part2);
    len_result = len1 + len2;
    result = KFS_MALLOC(len_result + 1);
    if (result != NULL) {
        memcpy(result, part1, len1);
        memcpy(result + len1, part2, len2);
        result[len_result] = '\0';
    }

    KFS_RETURN(result);
}

/**
 * Concatenate two strings, preferably using given buffer. If the supplied
 * buffer is not large enough a new character array is allocated which must
 * eventually be freed. Check this by comparing the argument with the return
 * value. If memory allocation fails, NULL is returned.
 */
char *
kfs_bufstrcat(char *buf, const char *part1, const char *part2, size_t bufsize)
{
    size_t len1 = 0;
    size_t len2 = 0;
    size_t len_total = 0;

    KFS_ENTER();

    KFS_ASSERT(buf != NULL && part1 != NULL && part2 != NULL);
    len1 = strlen(part1);
    len2 = strlen(part2);
    len_total = len1 + len2;
    if (len_total >= bufsize) {
        /* The given buffer is too small. */
        buf = KFS_MALLOC(len_total + 1);
        if (buf == NULL) {
            KFS_RETURN(NULL);
        }
    }
    memcpy(buf, part1, len1);
    memcpy(buf + len1, part2, len2);
    buf[len_total] = '\0';

    KFS_RETURN(buf);
}

/**
 * Allocate memory for a new string and copy the given data into it.
 */
char *
kfs_strcpy(const char *src)
{
    size_t len = 0;
    char *dest = NULL;

    KFS_ENTER();

    KFS_ASSERT(src != NULL);
    len = strlen(src) + 1; /* +1 for the trailing '\0'. */
    dest = KFS_MALLOC(len);
    if (dest != NULL) {
        dest = memcpy(dest, src, len);
    }

    KFS_RETURN(dest);
}

/**
 * Like sprintf but does not require buffer: resulting message will always be in
 * freshly allocated buffer which must eventually be freed. Unless an error
 * occurred, in which case NULL is returned.
 */
char *
kfs_sprintf(const char *fmt, ...)
{
    char *buf = NULL;
    char *newbuf = NULL;
    size_t buflen = 255; /* Guess at maximum needed buflen (smallbin). */
    int ret = 0;
    va_list ap;

    KFS_ENTER();

    KFS_ASSERT(fmt != NULL);
    buf = KFS_MALLOC(buflen);
    if (buf == NULL) {
        /* Not even 100b are available: never mind the whole thing. */
        KFS_RETURN(NULL);
    }
    for (;;) {
        va_start(ap, fmt);
        ret = vsnprintf(buf, buflen, fmt, ap);
        va_end(ap);
        KFS_ASSERT(ret >= 0);
        if (ret < buflen) {
            KFS_RETURN(buf);
        }
        /* Reallocation allowed only once. */
        KFS_ASSERT(newbuf == NULL);
        buflen = ret + 1;
        newbuf = KFS_REALLOC(buf, buflen);
        if (newbuf == NULL) {
            buf = KFS_FREE(buf);
            KFS_RETURN(NULL);
        }
        buf = newbuf;
    }

    KFS_RETURN(NULL);
}

/**
 * Extract a key from the configuration file into a freshly allocated buffer.
 * Returns NULL if there is no such key (or no such section or configuration
 * file, or no memory, etc), returns a pointer to the buffer on success. This
 * pointer should eventually be freed with KFS_FREE().
 */
char *
kfs_ini_gets(const char *conffile, const char *section, const char *key)
{
    char *buf = NULL;
    char *newbuf = NULL;
    size_t buflen = 0;
    int ret = 0;

    KFS_ENTER();

    buflen = 256;
    buf = KFS_MALLOC(buflen);
    if (buf == NULL) {
        KFS_RETURN(NULL);
    }
    for (;;) {
        ret = ini_gets(section, key, "", buf, buflen, conffile);
        if (ret == 0) {
            buf = KFS_FREE(buf);
            KFS_RETURN(NULL);
        } else if (ret < buflen - 1) {
            KFS_RETURN(buf);
        }
        buflen *= 2;
        newbuf = KFS_REALLOC(buf, buflen);
        if (newbuf == NULL) {
            buf = KFS_FREE(buf);
            KFS_RETURN(NULL);
        } else {
            buf = newbuf;
        }
    }

    KFS_RETURN(NULL);
}

/**
 * Serialise a struct stat to an array of 13 uint32_t elements. Returns a
 * pointer to the array. Total size in bytes: 52.
 *
 * The elements are ordered as follows:
 *
 * - st_dev
 * - st_ino
 * - st_mode
 * - st_nlink
 * - st_uid
 * - st_gid
 * - st_rdev
 * - st_size
 * - st_blksize
 * - st_blocks
 * - st_atime
 * - st_mtime
 * - st_ctime
 *
 * This assumes that all those values are of a type that entirely fits in a
 * uint32_t. TODO: Check if that is always the case.
 */
uint32_t *
serialise_stat(uint32_t intbuf[13], const struct stat * const stbuf)
{
    KFS_ENTER();

    KFS_ASSERT(intbuf != NULL && stbuf != NULL);
    KFS_ASSERT(sizeof(uint32_t) == 4);
    intbuf[0] = htonl(stbuf->st_dev);
    intbuf[1] = htonl(stbuf->st_ino);
    intbuf[2] = htonl(stbuf->st_mode);
    intbuf[3] = htonl(stbuf->st_nlink);
    intbuf[4] = htonl(stbuf->st_uid);
    intbuf[5] = htonl(stbuf->st_gid);
    intbuf[6] = htonl(stbuf->st_rdev);
    intbuf[7] = htonl(stbuf->st_size);
    intbuf[8] = htonl(stbuf->st_blksize);
    intbuf[9] = htonl(stbuf->st_blocks);
    intbuf[10] = htonl(stbuf->st_atime);
    intbuf[11] = htonl(stbuf->st_mtime);
    intbuf[12] = htonl(stbuf->st_ctime);

    KFS_RETURN(intbuf);
}

/**
 * Counterpart to serialise_stat().
 */
struct stat *
unserialise_stat(struct stat * const stbuf, const uint32_t intbuf[13])
{
    KFS_ENTER();

    KFS_ASSERT(stbuf != NULL && intbuf != NULL);
    KFS_ASSERT(sizeof(uint32_t) == 4);
    stbuf->st_dev = ntohl(intbuf[0]);
    stbuf->st_ino = ntohl(intbuf[1]);
    stbuf->st_mode = ntohl(intbuf[2]);
    stbuf->st_nlink = ntohl(intbuf[3]);
    stbuf->st_uid = ntohl(intbuf[4]);
    stbuf->st_gid = ntohl(intbuf[5]);
    stbuf->st_rdev = ntohl(intbuf[6]);
    stbuf->st_size = ntohl(intbuf[7]);
    stbuf->st_blksize = ntohl(intbuf[8]);
    stbuf->st_blocks = ntohl(intbuf[9]);
    stbuf->st_atime = ntohl(intbuf[10]);
    stbuf->st_mtime = ntohl(intbuf[11]);
    stbuf->st_ctime = ntohl(intbuf[12]);

    KFS_RETURN(stbuf);
}
