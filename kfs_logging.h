#ifndef _KFS_LOGGING_H
#define _KFS_LOGGING_H

/**
 * Defines logging functions for the KennyFS project. Provides six logging
 * levels:
 * - DEBUG
 * - INFO
 * - WARNING
 * - ERROR
 * - CRITICAL
 * - SILENT
 * Logging functions are available for the first five levels: kfs_debug(),
 * kfs_info(), etc.  The KFS_LOGGING_LEVEL macro can be used to select which
 * functions are defined. If NDEBUG is defined the default logging level is
 * WARNING, otherwise it is DEBUG.
 */

#define DEBUG 0
#define INFO 1
#define WARNING 2
#define ERROR 3
#define CRITICAL 4
#define SILENT 5

#ifndef KFS_LOGGING_LEVEL
#ifdef NDEBUG
#define KFS_LOGGING_LEVEL WARNING
#else
#define KFS_LOGGING_LEVEL DEBUG
#endif
#elif KFS_LOGGING_LEVEL < DEBUG || DEBUG_LEVEL > SILENT
#error KFS_LOGGING_LEVEL has an illegal value.
#endif

#define kfs_do_nothing(format, ...)
#define kfs_log(level, fmt, ...) fprintf(stderr, "%s: " fmt, \
        level, ## __VA_ARGS__)


#if KFS_LOGGING_LEVEL == DEBUG
#undef kfs_log
#define kfs_log(level, fmt, ...) fprintf(stderr, "[%s] %s:%d %s(): " fmt, \
        level, __FILE__, __LINE__, __func__, ## __VA_ARGS__)
#define kfs_debug(...) kfs_log("DEBUG", __VA_ARGS)
#else
#define kfs_debug kfs_do_nothing
#endif

#if KFS_LOGGING_LEVEL <= INFO
#define kfs_info kfs_log("INFO", __VA_ARGS)
#else
#define kfs_info kfs_do_nothing
#endif

#if KFS_LOGGING_LEVEL <= WARNING
#define kfs_warning kfs_log("WARNING", __VA_ARGS)
#else
#define kfs_warning kfs_do_nothing
#endif

#if KFS_LOGGING_LEVEL <= ERROR
#define kfs_error kfs_log("ERROR", __VA_ARGS)
#else
#define kfs_error kfs_do_nothing
#endif

#if KFS_LOGGING_LEVEL <= CRITICAL
#define kfs_critical kfs_log("CRITICAL", __VA_ARGS)
#else
#define kfs_critical kfs_do_nothing
#endif

#undef kfs_do_nothing
#undef kfs_log

#endif /* _KFS_LOGGING_H */
