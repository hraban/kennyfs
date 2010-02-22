#ifndef _KFS_MISC_H
#define _KFS_MISC_H

#include <errno.h>
#include <string.h>

#define AR_SIZE(ar) (sizeof(ar) / sizeof((ar)[0]))

static inline int
min(int x, int y) {
    return x > y ? y : x;
}

/**
 * Concatenate two strings, preferably using given buffer. If the supplied
 * buffer is not large enough a new character array is allocated which must
 * eventually be freed. Check this by comparing the argument with the return
 * value. If memory allocation fails, NULL is returned and errno is set.
 */
static inline char *
kfs_bufstrcat(char *buf, const char *part1, const char *part2, size_t bufsize)
{
    /* Maybe the buffer is too small. */
    if (strlen(part1) + strlen(part2) >= bufsize) {
        buf = KFS_MALLOC(strlen(part1) + strlen(part2) + 1);
        if (buf == NULL) {
            KFS_ERROR("malloc: %s", strerror(errno));
            return NULL;
        }
    }
    if (buf != part1) {
        strcpy(buf, part1); /* Careful: buffer overflow prone. */
    }
    strcat(buf, part2); /* Re: careful. */

    return buf;
}

#endif // _KFS_MISC_H
