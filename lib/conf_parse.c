#include "conf_parse.h"
#include "log.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void rtrim(char *s)
{
    size_t n = strlen(s);
    while (n > 0 && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
}

void conf_parse(const char *path, const char *log_prefix,
                conf_handler_fn handler, void *userdata)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        if (errno == ENOENT)
            log_info("%s: %s not found, using defaults", log_prefix, path);
        else
            log_warn("%s: cannot open %s: %s", log_prefix, path, strerror(errno));
        return;
    }
    log_info("%s: loading %s", log_prefix, path);

    char line[1024];
    int  lineno = 0;
    while (fgets(line, sizeof(line), f)) {
        lineno++;
        rtrim(line);

        /* Skip leading whitespace, blank lines, and comments. */
        char *p = line;
        while (*p && isspace((unsigned char)*p))
            p++;
        if (*p == '\0' || *p == '#')
            continue;

        /* Split on the first '='. */
        char *eq = strchr(p, '=');
        if (!eq) {
            log_warn("%s: %s:%d: missing '=', skipping", log_prefix, path, lineno);
            continue;
        }
        *eq = '\0';
        char *key = p;
        char *val = eq + 1;

        rtrim(key);
        while (*val && isspace((unsigned char)*val))
            val++;

        handler(path, lineno, key, val, userdata);
    }

    fclose(f);
}

int conf_parse_nonneg_int(const char *path, int lineno, const char *log_prefix,
                          const char *key, const char *val,
                          long max, int *out)
{
    char *end;
    long v = strtol(val, &end, 10);
    if (*end != '\0' || v < 0 || v > max) {
        log_warn("%s: %s:%d: invalid %s '%s', using default",
                 log_prefix, path, lineno, key, val);
        return 0;
    }
    *out = (int)v;
    return 1;
}

void conf_copy_str(const char *path, int lineno, const char *log_prefix,
                   const char *key, const char *val,
                   char *dst, size_t dst_size)
{
    size_t vlen = strlen(val);
    if (vlen >= dst_size) {
        log_warn("%s: %s:%d: %s value too long (%zu bytes, max %zu), truncating",
                 log_prefix, path, lineno, key, vlen, dst_size - 1);
    }
    snprintf(dst, dst_size, "%s", val);
}
