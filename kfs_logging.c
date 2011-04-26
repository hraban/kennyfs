#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/time.h>

#include "kfs.h"
#include "kfs_logging.h"

enum kfs_loglevel kfs_loglevel = KFS_LOGLVL_WARNING;

/**
 * Log message according to global logging policy and given loglevel.
 *
 * Only logs a message if the level for this message equals or exceeds the
 * global log level. Depending on the latter, a different output format is
 * employed. Information like the caller's filename, linenumber and function
 * name can be used in the output, depending on the global log level.
 */
void
kfs_log(enum kfs_loglevel level, FILE *logdest,
#ifdef KFS_LOG_ADVANCED
        const char *file, unsigned int line, const char *func,
#endif
        const char *fmt, ...)
{
    va_list args;
    struct timeval tv;
    const char *lvlstr = NULL;

    if (level < kfs_loglevel) {
        return;
    }
    /* The level of this particular log message. */
    switch (level) {
        case KFS_LOGLVL_TRACE:
            lvlstr = "trace";
            break;
        case KFS_LOGLVL_DEBUG:
            lvlstr = "debug";
            break;
        case KFS_LOGLVL_INFO:
            lvlstr = "info";
            break;
        case KFS_LOGLVL_WARNING:
            lvlstr = "WARNING";
            break;
        case KFS_LOGLVL_ERROR:
            lvlstr = "ERROR";
            break;
        case KFS_LOGLVL_CRITICAL:
            lvlstr = "CRITICAL";
            break;
        default:
            fprintf(logdest, "kfs_log called with illegal level.\n");
            abort();
            break;
    }
    gettimeofday(&tv, NULL);
    /* The global logging level (influences output formatting). */
    switch (kfs_loglevel) {
        case KFS_LOGLVL_TRACE:
        case KFS_LOGLVL_DEBUG:
            fprintf(logdest, "%010jd.%06ld [kfs_%s]"
#ifdef KFS_LOG_ADVANCED
                    " %s:%u %s"
#endif
                    ": ", (intmax_t) tv.tv_sec, tv.tv_usec, lvlstr
#ifdef KFS_LOG_ADVANCED
                    , file, line, func
#endif
                    );
            break;
        case KFS_LOGLVL_INFO:
        case KFS_LOGLVL_WARNING:
        case KFS_LOGLVL_ERROR:
        case KFS_LOGLVL_CRITICAL:
            fprintf(logdest, "kfs_%s: ", lvlstr);
            break;
        default:
            fprintf(logdest, "kfs_loglevel has illegal value.\n");
            abort();
            break;
    }

    va_start(args, fmt);
    vfprintf(logdest, fmt, args);
    va_end(args);

    fprintf(logdest, "\n");

    return;
}
