#pragma once

#include <stddef.h>

/*
 * conf_parse.h — shared key=value config file parser.
 *
 * Format: one key=value per line, '#' comments, blank lines ignored,
 * leading/trailing whitespace around key and value stripped.
 * Unknown keys are warned and skipped; missing file falls back silently.
 *
 * Call conf_parse() once per file; it invokes handler for every valid
 * key=value pair.  Use conf_parse_nonneg_int() and conf_copy_str() inside
 * the handler to extract typed values.
 *
 * log_prefix is prepended to all messages (e.g. "config", "greeter config").
 */

/* Called for each key=value pair found in the file. */
typedef void (*conf_handler_fn)(const char *path, int lineno,
                                const char *key, const char *val,
                                void *userdata);

void conf_parse(const char *path, const char *log_prefix,
                conf_handler_fn handler, void *userdata);

/* Parse a non-negative integer from val into *out.  max is inclusive.
 * Returns 1 on success, 0 on failure (logs a warning). */
int  conf_parse_nonneg_int(const char *path, int lineno, const char *log_prefix,
                           const char *key, const char *val,
                           long max, int *out);

/* Copy val into dst (size dst_size), logging a warning if truncated. */
void conf_copy_str(const char *path, int lineno, const char *log_prefix,
                   const char *key, const char *val,
                   char *dst, size_t dst_size);
