#include "sessions.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ini.h"
#include "lib/log.h"

#define SEAT_STATE_DIR "/var/lib/atrium/seats"

#define SESSIONS_MAX 64

/* Where to look for session files */
static const char *session_dirs[] = {
    "/usr/local/share/wayland-sessions",
    "/usr/share/wayland-sessions",
};
#define N_SESSION_DIRS ((int)(sizeof(session_dirs) / sizeof(session_dirs[0])))

static session_entry g_sessions[SESSIONS_MAX];
static int           g_session_count = 0;

/* inih callback context for parsing a single .desktop file. */
typedef struct {
    session_entry *entry;
    int            skip;
    int            oom;
} parse_ctx;

/* Convert semicolon-separated list to colon-separated (XDG_CURRENT_DESKTOP
format). Strip trailing colon if present. */
static char *convert_semicolons(const char *input) {
    char *out = strdup(input);
    if (!out)
        return NULL;
    for (char *p = out; *p; p++)
        if (*p == ';')
            *p = ':';
    size_t len = strlen(out);
    if (len > 0 && out[len - 1] == ':')
        out[len - 1] = '\0';
    return out;
}

/* Checks whether cmd exists and is executable. Returns 1 on success. */
static int try_exec_ok(const char *cmd) {
    if (cmd[0] == '/') /* Handle absolute path. */
        return access(cmd, X_OK) == 0;

    /* Handle relative path. */
    for (const char *p = getenv("PATH"), *end = p; p && *p; p = end ? end + 1 : NULL) {
        end = strchr(p, ':');
        size_t dirlen = end ? (size_t)(end - p) : strlen(p);
        char   full[dirlen + 1 + strlen(cmd) + 1];
        snprintf(full, sizeof(full), "%.*s/%s", (int)dirlen, p, cmd);
        if (access(full, X_OK) == 0)
            return 1;
    }
    return 0;
}

/* inih callback for parsing a .desktop file entry. */
static int on_desktop_key(void *userdata, const char *section, const char *name,
                          const char *value) {
    if (strcmp(section, "Desktop Entry") != 0)
        return 1;

    parse_ctx *ctx = userdata;

    if (strcmp(name, "Name") == 0 && !ctx->entry->name) {
        ctx->entry->name = strdup(value);
        if (!ctx->entry->name)
            goto oom;
    } else if (strcmp(name, "Exec") == 0 && !ctx->entry->exec) {
        ctx->entry->exec = strdup(value);
        if (!ctx->entry->exec)
            goto oom;
    } else if (strcmp(name, "DesktopNames") == 0 && !ctx->entry->desktop_names) {
        ctx->entry->desktop_names = convert_semicolons(value);
        if (!ctx->entry->desktop_names)
            goto oom;
    } else if (strcmp(name, "TryExec") == 0 && !try_exec_ok(value)) {
        log_info("sessions: TryExec '%s' not found", value);
        ctx->skip = 1;
    } else if ((strcmp(name, "Hidden") == 0 || strcmp(name, "NoDisplay") == 0) &&
               strcmp(value, "true") == 0) {
        ctx->skip = 1;
    } else if (strcmp(name, "Type") == 0 && strcmp(value, "Application") != 0) {
        ctx->skip = 1;
    }
    /* Locale-suffixed keys (e.g. Name[de]=) are silently ignored. */
    return 1;

oom:
    ctx->oom = 1;
    return 0;
}

static void free_entry(session_entry *e) {
    free(e->id);
    free(e->name);
    free(e->exec);
    free(e->desktop_names);
    memset(e, 0, sizeof(*e));
}

static int parse_desktop_file(const char *path, const char *id, session_entry *entry) {
    memset(entry, 0, sizeof(*entry));

    entry->id = strdup(id);
    if (!entry->id) {
        log_error("sessions: out of memory");
        return -1;
    }

    parse_ctx ctx = {.entry = entry};

    int r = ini_parse(path, on_desktop_key, &ctx);
    if (r < 0) {
        log_syserr("sessions: open %s", path);
        goto err;
    }
    if (r > 0)
        log_warn("sessions: parse error in %s at line %d", path, r);

    if (ctx.oom) {
        log_error("sessions: out of memory parsing %s", path);
        goto err;
    }
    if (ctx.skip || !entry->name || !entry->exec) {
        log_debug("sessions: skipping '%s' from %s", id, path);
        goto err;
    }

    return 0;

err:
    free_entry(entry);
    return -1;
}

void sessions_free(void) {
    for (int i = 0; i < g_session_count; i++)
        free_entry(&g_sessions[i]);
    g_session_count = 0;
}

int sessions_scan(void) {
    sessions_free();

    for (int d = 0; d < N_SESSION_DIRS; d++) {
        DIR *dir = opendir(session_dirs[d]);
        if (!dir) {
            log_debug("sessions: %s: %s", session_dirs[d], strerror(errno));
            continue;
        }

        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            if (g_session_count >= SESSIONS_MAX) {
                log_warn("sessions: list full at %d entries, stopping", SESSIONS_MAX);
                break;
            }

            const char *name = ent->d_name;
            size_t      namelen = strlen(name);

            if (namelen < 9 || strcmp(name + namelen - 8, ".desktop") != 0)
                continue;

            /* Derive session id: filename without ".desktop" */
            char   id[64];
            size_t idlen = namelen - 8;
            if (idlen >= sizeof(id))
                idlen = sizeof(id) - 1;
            memcpy(id, name, idlen);
            id[idlen] = '\0';

            /* Earlier directory wins: skip duplicates from later dirs. */
            if (sessions_find(id)) {
                log_debug("sessions: skip duplicate '%s' from %s", id, session_dirs[d]);
                continue;
            }

            char path[512];
            snprintf(path, sizeof(path), "%s/%s", session_dirs[d], name);

            session_entry entry;
            if (parse_desktop_file(path, id, &entry) == 0) {
                g_sessions[g_session_count++] = entry;
                log_debug("sessions: loaded '%s' (id='%s' exec='%s')", entry.name, entry.id,
                          entry.exec);
            }
        }

        closedir(dir);
    }

    if (g_session_count == 0)
        log_warn("sessions: no Wayland sessions found");
    else
        log_info("sessions: %d session(s) available", g_session_count);

    return g_session_count;
}

const session_entry *sessions_first(void) { return g_session_count > 0 ? &g_sessions[0] : NULL; }

const session_entry *sessions_next(const session_entry *s) {
    int idx = (int)(s - g_sessions);
    return (idx + 1 < g_session_count) ? &g_sessions[idx + 1] : NULL;
}

const session_entry *sessions_find(const char *id) {
    for (int i = 0; i < g_session_count; i++) {
        if (strcmp(g_sessions[i].id, id) == 0)
            return &g_sessions[i];
    }
    return NULL;
}

/* Reject seat names that could cause directory traversal or other path injection. */
static int is_safe_name(const char *name) {
    if (!name || name[0] == '\0')
        return 0;
    for (const char *p = name; *p; p++) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9') ||
              *p == '-' || *p == '_'))
            return 0;
    }
    return 1;
}

void sessions_save_seat(const char *seat_name, const char *session_id) {
    if (!is_safe_name(seat_name)) {
        log_warn("sessions_save_seat: unsafe seat name '%s', skipping", seat_name);
        return;
    }
    if (mkdir(SEAT_STATE_DIR, 0700) < 0 && errno != EEXIST) {
        log_syserr("sessions_save_seat: mkdir %s", SEAT_STATE_DIR);
        return;
    }
    char path[256], tmp_path[272];
    snprintf(path, sizeof(path), "%s/%s", SEAT_STATE_DIR, seat_name);
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", path);

    FILE *f = fopen(tmp_path, "w");
    if (!f) {
        log_syserr("sessions_save_seat: fopen %s", tmp_path);
        return;
    }
    fprintf(f, "%s\n", session_id);
    fclose(f);

    if (rename(tmp_path, path) < 0) {
        log_syserr("sessions_save_seat: rename %s -> %s", tmp_path, path);
        return;
    }
    log_debug("sessions_save_seat: saved '%s' for seat '%s'", session_id, seat_name);
}

void sessions_load_seat(const char *seat_name, char *buf, size_t buflen) {
    buf[0] = '\0';
    if (!is_safe_name(seat_name))
        return;
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", SEAT_STATE_DIR, seat_name);

    FILE *f = fopen(path, "r");
    if (!f)
        return;
    if (!fgets(buf, (int)buflen, f)) {
        buf[0] = '\0';
        fclose(f);
        return;
    }
    fclose(f);

    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n')
        buf[len - 1] = '\0';
    if (buf[0] == '\0')
        return;
    if (!sessions_find(buf)) {
        log_info("sessions_load_seat: saved session '%s' for seat '%s' is no longer installed", buf,
                 seat_name);
        buf[0] = '\0';
        return;
    }
    log_debug("sessions_load_seat: preselecting '%s' for seat '%s'", buf, seat_name);
}
