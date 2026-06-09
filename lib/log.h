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

/* Internal helper macro. The prio prefix (e.g. "<3>") is parsed by the systemd
journal to set the PRIORITY field; it is stripped from the message. */
#define _log(prio, level, fmt, ...)                                               \
    do {                                                                          \
        fprintf(stderr, prio LOG_PREFIX " [" level "] " fmt "\n", ##__VA_ARGS__); \
    } while (0)

#define log_info(...)  _log("<6>", "info", __VA_ARGS__)
#define log_warn(...)  _log("<4>", "warning", __VA_ARGS__)
#define log_error(...) _log("<3>", "error", __VA_ARGS__)

/* Replacement for perror(): appends a description of errno. */
#define log_syserr(fmt, ...) _log("<3>", "error", fmt ": %s", ##__VA_ARGS__, strerror(errno))

/* Debug messages (compile-time conditional). */
#ifdef ATRIUM_DEBUG
#define log_debug(...) _log("<7>", "debug", __VA_ARGS__)
#else
#define log_debug(...) ((void)0)
#endif
