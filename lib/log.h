#pragma once

/*
 * log.h - structured logging macros
 *
 * Debug logging can be toggled with -DATRIUM_DEBUG.
 */

#include <errno.h>
#include <stdio.h>
#include <string.h>

#ifndef LOG_PREFIX
#define LOG_PREFIX "atrium"
#endif

/* Internal helper macro. */
#define _log(level, fmt, ...)                                                                      \
    do {                                                                                           \
        fprintf(stderr, LOG_PREFIX " [" level "] " fmt "\n", ##__VA_ARGS__);                       \
    } while (0)

#define log_info(...) _log("info", __VA_ARGS__)
#define log_warn(...) _log("warning", __VA_ARGS__)
#define log_error(...) _log("error", __VA_ARGS__)

/* Replacement for perror(): appends a description of errno. */
#define log_syserr(fmt, ...) _log("error", fmt ": %s", ##__VA_ARGS__, strerror(errno))

/* Debug messages (compile-time conditional). */
#ifdef ATRIUM_DEBUG
#define log_debug(...) _log("debug", __VA_ARGS__)
#else
#define log_debug(...) ((void)0)
#endif
