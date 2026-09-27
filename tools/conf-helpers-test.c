/*
 * conf-helpers-test.c - test cases for the helpers in lib/conf_helpers.c
 *
 * Prints one line per case and exits non-zero if any case failed.
 *
 * Usage: build/atrium-conf-helpers-test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "lib/conf_helpers.h"

static int g_fails;

static void check_str(const char *desc, char *actual, const char *expected) {
    int ok = expected ? (actual && strcmp(actual, expected) == 0) : (actual == NULL);
    printf("%-5s %-34s actual [%s]\n", ok ? "PASS" : "FAIL", desc, actual ? actual : "(null)");
    if (!ok) {
        printf("      ^ expected [%s]\n", expected ? expected : "(null)");
        g_fails++;
    }
    free(actual);
}

static void check_int(const char *desc, int actual, int expected) {
    int ok = (actual == expected);
    printf("%-5s %-34s actual %d\n", ok ? "PASS" : "FAIL", desc, actual);
    if (!ok) {
        printf("      ^ expected %d\n", expected);
        g_fails++;
    }
}

static void write_file(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }
    fputs(content, f);
    fclose(f);
}

static void test_file_lookup(const char *dir) {
    char defs[512], conf[512], missing[512];
    snprintf(defs, sizeof(defs), "%s/login.defs", dir);
    snprintf(conf, sizeof(conf), "%s/vconsole.conf", dir);
    snprintf(missing, sizeof(missing), "%s/does-not-exist", dir);

    /* "key value" layout, as in /etc/login.defs. */
    write_file(defs, "# a comment\n"
                     "\n"
                     "ENV_SUPATH\tPATH=/sbin:/usr/sbin\n"
                     "ENV_PATH\tPATH=/usr/local/bin:/usr/bin\n"
                     "ENV_PATH_EXTRA\tPATH=/wrong\n"
                     "   INDENTED   indented value   \n"
                     "UMASK 022\n"
                     "EMPTY\n");

    /* "key=value" layout, as in /etc/vconsole.conf and /etc/locale.conf. */
    write_file(conf, "KEYMAP=us\n"
                     "XKBLAYOUT=\"us,us,de\"\n"
                     "XKBVARIANT=,alt-intl,\n"
                     "SPACED = spaced value \n"
                     "QUOTED_PAD=\"  pad  \"\n"
                     "NOVALUE=\n");

    puts("-- conf_file_lookup, \"key value\" layout");
    check_str("ENV_PATH", conf_file_lookup(defs, "ENV_PATH"), "PATH=/usr/local/bin:/usr/bin");
    check_str("ENV_SUPATH", conf_file_lookup(defs, "ENV_SUPATH"), "PATH=/sbin:/usr/sbin");
    check_str("UMASK", conf_file_lookup(defs, "UMASK"), "022");
    check_str("indented key, padded value", conf_file_lookup(defs, "INDENTED"), "indented value");
    check_str("key with no value", conf_file_lookup(defs, "EMPTY"), NULL);
    check_str("absent key", conf_file_lookup(defs, "NOPE"), NULL);
    check_str("ENV_PAT (prefix of ENV_PATH)", conf_file_lookup(defs, "ENV_PAT"), NULL);

    puts("-- conf_file_lookup, \"key=value\" layout");
    check_str("KEYMAP", conf_file_lookup(conf, "KEYMAP"), "us");
    check_str("quoted value", conf_file_lookup(conf, "XKBLAYOUT"), "us,us,de");
    check_str("value with commas", conf_file_lookup(conf, "XKBVARIANT"), ",alt-intl,");
    check_str("spaces around '='", conf_file_lookup(conf, "SPACED"), "spaced value");
    check_str("quotes keep inner padding", conf_file_lookup(conf, "QUOTED_PAD"), "  pad  ");
    check_str("empty value", conf_file_lookup(conf, "NOVALUE"), NULL);

    puts("-- conf_file_lookup, missing file");
    check_str("unreadable path", conf_file_lookup(missing, "KEYMAP"), NULL);

    unlink(defs);
    unlink(conf);
}

static void test_parse_int(void) {
    int v;

    puts("-- conf_parse_int");
    v = -1;
    check_int("\"42\" accepted", conf_parse_int("t", "k", "42", 100, &v), 1);
    check_int("\"42\" value", v, 42);

    v = -1;
    check_int("\"150\" over max accepted", conf_parse_int("t", "k", "150", 100, &v), 1);
    check_int("\"150\" clamped to max", v, 100);

    v = 7;
    check_int("\"abc\" rejected", conf_parse_int("t", "k", "abc", 100, &v), 0);
    check_int("\"abc\" leaves out untouched", v, 7);

    v = 7;
    check_int("\"12x\" rejected", conf_parse_int("t", "k", "12x", 100, &v), 0);
    v = 7;
    check_int("\"-1\" rejected", conf_parse_int("t", "k", "-1", 100, &v), 0);
    v = 7;
    check_int("\"0\" accepted", conf_parse_int("t", "k", "0", 100, &v), 1);
    check_int("\"0\" value", v, 0);

    /* An empty value falls back to the default rather than silently returning
    zero. */
    v = 7;
    check_int("\"\" rejected", conf_parse_int("t", "k", "", 100, &v), 0);
    check_int("\"\" leaves out untouched", v, 7);

    v = 7;
    check_int("whitespace-only rejected", conf_parse_int("t", "k", "   ", 100, &v), 0);
    check_int("whitespace-only leaves out", v, 7);

    /* Surrounding whitespace is ignored. */
    v = -1;
    check_int("\" 42 \" accepted", conf_parse_int("t", "k", " 42 ", 100, &v), 1);
    check_int("\" 42 \" value", v, 42);
    v = 7;
    check_int("\" -1 \" rejected", conf_parse_int("t", "k", " -1 ", 100, &v), 0);
    v = 7;
    check_int("\" 12x \" rejected", conf_parse_int("t", "k", " 12x ", 100, &v), 0);
}

static void test_parse_bool(void) {
    int v;

    puts("-- conf_parse_bool");
    const char *truthy[] = {"true", "yes", "1", "on"};
    for (size_t i = 0; i < sizeof(truthy) / sizeof(*truthy); i++) {
        v = -1;
        char desc[64];
        snprintf(desc, sizeof(desc), "\"%s\" is true", truthy[i]);
        if (conf_parse_bool("t", "k", truthy[i], &v) != 1)
            v = -1;
        check_int(desc, v, 1);
    }

    const char *falsy[] = {"false", "no", "0", "off"};
    for (size_t i = 0; i < sizeof(falsy) / sizeof(*falsy); i++) {
        v = -1;
        char desc[64];
        snprintf(desc, sizeof(desc), "\"%s\" is false", falsy[i]);
        if (conf_parse_bool("t", "k", falsy[i], &v) != 1)
            v = -1;
        check_int(desc, v, 0);
    }

    /* Surrounding whitespace is ignored. */
    v = -1;
    check_int("\" true \" is true", conf_parse_bool("t", "k", " true ", &v), 1);
    check_int("\" true \" value", v, 1);
    v = -1;
    check_int("\"\\toff\\t\" is false", conf_parse_bool("t", "k", "\toff\t", &v), 1);
    check_int("\"\\toff\\t\" value", v, 0);
    v = 9;
    check_int("whitespace-only rejected", conf_parse_bool("t", "k", "  ", &v), 0);

    /* Documented as case-sensitive. */
    v = 9;
    check_int("\"TRUE\" rejected", conf_parse_bool("t", "k", "TRUE", &v), 0);
    check_int("\"TRUE\" leaves out untouched", v, 9);
    v = 9;
    check_int("\"maybe\" rejected", conf_parse_bool("t", "k", "maybe", &v), 0);
}

static void test_copy_str(void) {
    char dst[8];

    puts("-- conf_copy_str");
    conf_copy_str("t", "k", "short", dst, sizeof(dst));
    check_str("value that fits", strdup(dst), "short");

    conf_copy_str("t", "k", "far too long to fit", dst, sizeof(dst));
    check_str("value truncated to dst_size-1", strdup(dst), "far too");

    conf_copy_str("t", "k", "", dst, sizeof(dst));
    check_str("empty value", strdup(dst), "");
}

static void test_append_strlist(void) {
#define MAX_ITEMS 3
#define ITEM_SIZE 8
    char list[MAX_ITEMS][ITEM_SIZE];
    int  count = 0;

    puts("-- conf_append_strlist");
    conf_append_strlist("t", "k", "one", list[0], &count, MAX_ITEMS, ITEM_SIZE);
    conf_append_strlist("t", "k", "two", list[0], &count, MAX_ITEMS, ITEM_SIZE);
    check_int("count after two appends", count, 2);
    check_str("first item", strdup(list[0]), "one");
    check_str("second item", strdup(list[1]), "two");

    conf_append_strlist("t", "k", "toolongvalue", list[0], &count, MAX_ITEMS, ITEM_SIZE);
    check_int("count after third append", count, 3);
    check_str("third item truncated", strdup(list[2]), "toolong");

    /* Full: the next append must be ignored rather than overflow. */
    conf_append_strlist("t", "k", "four", list[0], &count, MAX_ITEMS, ITEM_SIZE);
    check_int("count unchanged when full", count, 3);
    check_str("last item unchanged", strdup(list[2]), "toolong");
#undef MAX_ITEMS
#undef ITEM_SIZE
}

int main(void) {
    /* Line-buffer stdout so the helpers' warnings on stderr interleave with the
    cases that provoked them, rather than clumping when output is piped. */
    setvbuf(stdout, NULL, _IOLBF, 0);

    const char *tmp = getenv("TMPDIR");
    char        dir[256];
    snprintf(dir, sizeof(dir), "%s/atrium-conf-test-XXXXXX", tmp && *tmp ? tmp : "/tmp");
    if (!mkdtemp(dir)) {
        perror("mkdtemp");
        return EXIT_FAILURE;
    }

    test_file_lookup(dir);
    test_parse_int();
    test_parse_bool();
    test_copy_str();
    test_append_strlist();

    rmdir(dir);

    printf("\n%s\n", g_fails ? "FAILED" : "all checks passed");
    return g_fails ? EXIT_FAILURE : EXIT_SUCCESS;
}
