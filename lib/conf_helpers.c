#include "conf_helpers.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "log.h"

bool conf_is_seat_section(const char *section, const char *seat) {
    return seat && *seat && strcmp(section, seat) == 0;
}

char *conf_file_lookup(const char *path, const char *key) {
    size_t keylen = strlen(key);

    FILE *f = fopen(path, "r");
    if (!f)
        return NULL;

    char  *line = NULL;
    char  *result = NULL;
    size_t cap = 0;

    while (getline(&line, &cap, f) > 0) {
        char *p = line;
        while (*p == ' ' || *p == '\t')
            ++p;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0')
            continue;
        if (strncmp(p, key, keylen) != 0)
            continue;
        p += keylen;

        /* Require a separator, so "KEY" does not match "KEY_EXTRA". */
        if (*p != ' ' && *p != '\t' && *p != '=')
            continue;

        while (*p == ' ' || *p == '\t')
            ++p;
        if (*p == '=') /* Consume the optional '=' separator. */
            ++p;
        while (*p == ' ' || *p == '\t')
            ++p;

        char *end = p + strlen(p);
        while (end > p && (end[-1] == '\n' || end[-1] == '\r' || end[-1] == ' ' || end[-1] == '\t'))
            --end;
        *end = '\0';

        /* If the value is quoted, remove the quotes. */
        if (*p == '"' && end > p + 1 && end[-1] == '"') {
            ++p;
            end[-1] = '\0';
        }
        if (!*p)
            continue;

        result = strdup(p);
        if (!result)
            log_error("conf_file_lookup: out of memory");
        break;
    }

    free(line);
    fclose(f);
    return result;
}

int conf_parse_int(const char *prefix, const char *key, const char *val, long max, int *out) {
    char *end;
    long  v = strtol(val, &end, 10);
    int   converted = (end != val); /* end == val means nothing was converted. */
    while (isspace((unsigned char)*end))
        ++end;
    if (!converted || *end != '\0' || v < 0) {
        log_warn("%s: invalid value for '%s': '%s', using default", prefix, key, val);
        return 0;
    }
    if (v > max) {
        log_warn("%s: value for '%s' (%ld) exceeds maximum (%ld), clamping", prefix, key, v, max);
        v = max;
    }
    *out = (int)v;
    return 1;
}

/* Return 1 if val equals expected, ignoring whitespace around val. */
static int val_eq(const char *val, const char *expected) {
    while (isspace((unsigned char)*val))
        ++val;
    size_t len = strlen(expected);
    if (strncmp(val, expected, len) != 0)
        return 0;
    val += len;
    while (isspace((unsigned char)*val))
        ++val;
    return *val == '\0';
}

int conf_parse_bool(const char *prefix, const char *key, const char *val, int *out) {
    if (val_eq(val, "true") || val_eq(val, "yes") || val_eq(val, "1") || val_eq(val, "on")) {
        *out = 1;
        return 1;
    }
    if (val_eq(val, "false") || val_eq(val, "no") || val_eq(val, "0") || val_eq(val, "off")) {
        *out = 0;
        return 1;
    }
    log_warn("%s: invalid boolean value for '%s': '%s', using default", prefix, key, val);
    return 0;
}

void conf_append_strlist(const char *prefix, const char *key, const char *val, char *list,
                         int *count, int max, size_t item_size) {
    if (*count >= max) {
        log_warn("%s: too many '%s' entries, ignoring '%s'", prefix, key, val);
        return;
    }
    size_t vlen = strlen(val);
    if (vlen >= item_size)
        log_warn("%s: '%s' value too long, truncating", prefix, key);
    snprintf(list + (size_t)(*count) * item_size, item_size, "%s", val);
    (*count)++;
}

void conf_copy_str(const char *prefix, const char *key, const char *val, char *dst,
                   size_t dst_size) {
    size_t vlen = strlen(val);
    if (vlen >= dst_size)
        log_warn("%s: value for '%s' too long (%zu bytes, max %zu), truncating", prefix, key, vlen,
                 dst_size - 1);
    snprintf(dst, dst_size, "%s", val);
}
