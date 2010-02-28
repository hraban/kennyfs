/**
 * Some utility functions.
 */

#include "kfs.h"
#include "kfs_memory.h"
#include "kfs_misc.h"

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
    size_t lentotal = 0;

    KFS_ASSERT(buf != NULL && part1 != NULL && part2 != NULL);
    len1 = strlen(part1);
    len2 = strlen(part2);
    lentotal = len1 + len2;
    /* Maybe the buffer is too small. */
    if (lentotal >= bufsize) {
        buf = KFS_MALLOC(lentotal + 1);
        if (buf == NULL) {
            KFS_ERROR("malloc: %s", strerror(errno));
        }
    }
    if (buf != NULL) {
        /* If the buffer was relocated, copy the first part as well. */
        if (buf != part1) {
            buf = memcpy(buf, part1, len1);
        }
        memcpy(buf + len1, part2, len2);
    }
    buf[lentotal] = '\0';

    return buf;
}

/**
 * Allocate memory for a new string and copy the given data into it.
 */
char *
kfs_strcpy(const char *src)
{
    size_t len = 0;
    char *dest = NULL;

    KFS_ASSERT(src != NULL);
    len = strlen(src);
    dest = KFS_MALLOC(len);
    if (dest != NULL) {
        dest = memcpy(dest, src, len);
    }

    return dest;
}
