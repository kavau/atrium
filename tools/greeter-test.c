/*
 * tools/greeter-test.c — standalone greeter IPC test.
 *
 * Takes the role of the daemon: sets up credential and result pipes, forks
 * and execs atrium-greeter, reads the submitted credentials, and replies
 * with "ok\n" or "fail:<reason>\n".  This validates the greeter's IPC
 * protocol without involving the daemon, logind, or PAM.
 *
 * Usage:
 *   ./build/atrium-greeter-test [/path/to/atrium-greeter]
 *
 * The optional argument overrides the greeter binary path.  When omitted,
 * ./build/atrium-greeter is used.
 *
 * Behaviour:
 *   - The greeter window appears.
 *   - On submit, this tool prints the received username and password.
 *   - If the username is "fail", it replies "fail:test error\n" to exercise
 *     the error path.  Otherwise it replies "ok\n".
 *   - On "ok" the greeter exits; this tool exits 0.
 *   - If the greeter is closed without submitting, this tool exits 1.
 *
 * IPC protocol:
 *   greeter → here : CREDENTIALS_FD: "<username>\0<password>\0"
 *   here → greeter : RESULT_FD:      "ok\n"  |  "fail:<reason>\n"
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

/*
 * read_field() — read one null-terminated field from fd into buf.
 *
 * Reads byte-by-byte until a '\0' is found or the buffer is exhausted.
 * Returns 1 on success, 0 on EOF or error.
 */
static int read_field(int fd, char *buf, size_t maxlen)
{
    size_t n = 0;
    while (n < maxlen) {
        char    c;
        ssize_t r = read(fd, &c, 1);
        if (r <= 0)
            return 0;
        buf[n++] = c;
        if (c == '\0')
            return 1;
    }
    return 0; /* buffer exhausted without finding '\0' */
}

/*
 * greeter_path() — resolve the greeter binary to exec.
 *
 * Priority: argv[1] > ./build/atrium-greeter.
 */
static const char *greeter_path(int argc, char **argv)
{
    if (argc >= 2)
        return argv[1];
    return "./build/atrium-greeter";
}

int main(int argc, char **argv)
{
    const char *greeter = greeter_path(argc, argv);

    /*
     * Two pipe pairs:
     *   creds[0]  = read-end  (we read)      creds[1]  = write-end (greeter writes)
     *   result[0] = read-end  (greeter reads) result[1] = write-end (we write)
     */
    int creds[2], result[2];
    if (pipe(creds) < 0 || pipe(result) < 0) {
        perror("pipe");
        return 1;
    }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        /*
         * Child: exec the greeter.
         *
         * Pass the fd numbers as env vars.  The fds must survive exec, so we
         * must NOT set O_CLOEXEC on them.  Close the unused ends before exec
         * so the greeter doesn't hold them open.
         */
        close(creds[0]);
        close(result[1]);

        char creds_str[16], result_str[16];
        snprintf(creds_str,  sizeof(creds_str),  "%d", creds[1]);
        snprintf(result_str, sizeof(result_str), "%d", result[0]);
        setenv("CREDENTIALS_FD", creds_str,  1);
        setenv("RESULT_FD",      result_str, 1);
        setenv("ATRIUM_USERNAME", "testuser", 1);

        execl(greeter, greeter, NULL);
        perror("execl");
        _exit(127);
    }

    /* Parent: we are the daemon stub. */
    close(creds[1]);    /* we only read from creds */
    close(result[0]);   /* we only write to result */

    int login_ok = 0;

    /*
     * Credential loop: receive credentials, decide pass/fail, reply.
     * If the username is "fail", simulate an auth failure so we can test
     * the greeter's error display and retry flow.
     */
    for (;;) {
        char username[256], password[256];

        if (!read_field(creds[0], username, sizeof(username))) {
            fprintf(stderr, "greeter-test: credentials pipe closed "
                    "(greeter exited?)\n");
            break;
        }
        if (!read_field(creds[0], password, sizeof(password))) {
            fprintf(stderr, "greeter-test: credentials pipe closed "
                    "mid-read\n");
            break;
        }

        fprintf(stderr, "greeter-test: received credentials: "
                "user='%s' pass='%s'\n", username, password);

        if (strcmp(username, "fail") == 0) {
            const char *reply = "fail:test error\n";
            fprintf(stderr, "greeter-test: replying fail (username is "
                    "'fail')\n");
            write(result[1], reply, strlen(reply));
            /* Keep pipes open so the greeter can retry. */
            continue;
        }

        fprintf(stderr, "greeter-test: replying ok\n");
        write(result[1], "ok\n", 3);
        login_ok = 1;
        break;
    }

    close(creds[0]);
    close(result[1]);

    int status;
    waitpid(pid, &status, 0);
    if (WIFEXITED(status))
        fprintf(stderr, "greeter-test: greeter exited with status %d\n",
                WEXITSTATUS(status));
    else
        fprintf(stderr, "greeter-test: greeter did not exit cleanly\n");

    return login_ok ? 0 : 1;
}
