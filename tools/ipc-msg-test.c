/*
 * ipc-msg-test.c - test cases for the typed-message helpers in lib/ipc.c

 * Usage: build/atrium-ipc-msg-test
 */

#include <stdio.h>
#include <string.h>

#include "lib/defs.h"
#include "lib/ipc.h"

static int g_fails;

static void check(const char *desc, int actual, int expected) {
    int ok = (actual == expected);
    printf("%-5s %-44s actual %d\n", ok ? "PASS" : "FAIL", desc, actual);
    if (!ok) {
        printf("      ^ expected %d\n", expected);
        g_fails++;
    }
}

static void check_str(const char *desc, const char *actual, const char *expected) {
    int ok = (actual && strcmp(actual, expected) == 0);
    printf("%-5s %-44s actual [%s]\n", ok ? "PASS" : "FAIL", desc, actual ? actual : "(null)");
    if (!ok) {
        printf("      ^ expected [%s]\n", expected);
        g_fails++;
    }
}

int main(void) {
    char        buf[64];
    const char *a, *b, *c;

    puts("-- ipc_msg_build");
    ssize_t n = ipc_msg_build3(buf, sizeof(buf), "cred", "dave", "pw", "cosmic");
    check("build3 length", (int)n, 5 + 5 + 3 + 7);
    check("build3 fields are NUL separated", memcmp(buf, "cred\0dave\0pw\0cosmic\0", 21), 0);

    n = ipc_msg_build1(buf, sizeof(buf), "power", "shutdown");
    check("build1 length", (int)n, 6 + 9);

    char small[8];
    check("build refuses to overflow",
          (int)ipc_msg_build3(small, sizeof(small), "cred", "dave", "pw", "cosmic"), -1);
    check("build fills the buffer exactly", (int)ipc_msg_build1(small, 7, "ab", "cde"), 7);
    check("build refuses one byte short", (int)ipc_msg_build1(small, 6, "ab", "cde"), -1);
    check("build refuses a buffer of zero", (int)ipc_msg_build1(small, 0, "ab", "cde"), -1);

    puts("-- ipc_msg_parse, well-formed");
    n = ipc_msg_build3(buf, sizeof(buf), "cred", "dave", "pw", "cosmic");
    check("parse3 accepts", ipc_msg_parse3(buf, n, "cred", &a, &b, &c), 0);
    check_str("  field 1", a, "dave");
    check_str("  field 2", b, "pw");
    check_str("  field 3", c, "cosmic");

    n = ipc_msg_build3(buf, sizeof(buf), "cred", "dave", "", "");
    check("parse3 accepts empty fields", ipc_msg_parse3(buf, n, "cred", &a, &b, &c), 0);
    check_str("  empty field is empty, not null", b, "");

    puts("-- ipc_msg_parse, rejections");
    n = ipc_msg_build3(buf, sizeof(buf), "cred", "dave", "pw", "cosmic");
    check("wrong tag", ipc_msg_parse3(buf, n, "power", &a, &b, &c), -1);
    check("too few args for the message", ipc_msg_parse2(buf, n, "cred", &a, &b), -1);
    check("truncated message", ipc_msg_parse3(buf, n - 1, "cred", &a, &b, &c), -1);
    check("zero length", ipc_msg_parse1(buf, 0, "cred", &a), -1);
    check("negative length", ipc_msg_parse1(buf, -1, "cred", &a), -1);

    char    two[64];
    ssize_t n2 = ipc_msg_build2(two, sizeof(two), "cred", "dave", "pw");
    check("too many args for the message", ipc_msg_parse3(two, n2, "cred", &a, &b, &c), -1);

    const char no_nul[] = "creddave";
    memcpy(buf, no_nul, sizeof(no_nul) - 1);
    check("unterminated tag", ipc_msg_parse1(buf, sizeof(no_nul) - 1, "cred", &a), -1);

    const char unterm_field[] = "cred\0dave";
    memcpy(buf, unterm_field, sizeof(unterm_field) - 1);
    check("unterminated field", ipc_msg_parse1(buf, sizeof(unterm_field) - 1, "cred", &a), -1);

    puts("-- ipc_msg_type");
    n = ipc_msg_build1(buf, sizeof(buf), IPC_TYPE_CRED, "x");
    check_str("reads the tag", ipc_msg_type(buf, n), IPC_TYPE_CRED);
    memcpy(buf, no_nul, sizeof(no_nul) - 1);
    check("null when unterminated", ipc_msg_type(buf, sizeof(no_nul) - 1) == NULL, 1);
    check("null when empty", ipc_msg_type(buf, 0) == NULL, 1);

    if (g_fails) {
        printf("\n%d check(s) failed\n", g_fails);
        return 1;
    }
    puts("\nall checks passed");
    return 0;
}
