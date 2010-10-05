/**
 * Some utility functions.
 */

#include <stdarg.h>
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
    size_t buflen = 100; /* Guess at maximum needed buflen. */
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
        newbuf = KFS_REALLOC(buf, buflen);
        buflen = ret + 1;
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
