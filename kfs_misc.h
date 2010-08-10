#ifndef KFS_MISC_H
#define KFS_MISC_H

#include <errno.h>
#include <string.h>

#include "kfs.h"
#include "kfs_logging.h"
#include "kfs_memory.h"

#define NUMELEM(ar) (sizeof(ar) / sizeof((ar)[0]))
#define KFS_SLEEP(n) kfs_sleep(n)

inline int min(int x, int y);
inline int max(int x, int y);
inline int xor(int x, int y);
inline unsigned int kfs_sleep(unsigned int seconds);
char * kfs_strcat(const char *part1, const char *part2);
char * kfs_bufstrcat(char *buf, const char *part1, const char *part2,
        size_t bufsize);
char * kfs_strcpy(const char *src);
char * kfs_ini_gets(const char *conffile, const char *section, const char *key);

#endif
