/*
 * env-dump.c - dump the environment a session is started with
 *
 * Usage: in /etc/atrium.conf,
 *   compositor = /path/to/atrium-env-dump /tmp/atrium-session-env
 * then log in and inspect the dump:
 *   grep -E 'XDG_DATA_DIRS|XDG_SEAT|XDG_SESSION_ID' /tmp/atrium-session-env
 *
 * Without an argument the environment goes to stderr, which the session runner
 * redirects to the journal.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

extern char **environ;

int main(int argc, char *argv[]) {
    FILE *out = stderr;

    if (argc > 1) {
        out = fopen(argv[1], "w");
        if (!out) {
            perror("env-dump: fopen");
            return EXIT_FAILURE;
        }
    }

    fprintf(out, "# env-dump pid %d uid %d\n", getpid(), (int)getuid());
    for (char **p = environ; *p; p++)
        fprintf(out, "%s\n", *p);

    if (out != stderr && fclose(out) != 0) {
        perror("env-dump: fclose");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
