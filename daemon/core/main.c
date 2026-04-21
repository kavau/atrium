#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <systemd/sd-bus.h>

/* 
 * SHORTCUT: Hardcoded session parameters
 */
#define UID  1000
#define SEAT "seat1"

/* Start a hardcoded graphical user session on seat1. */
static int create_session(void) {
    /* Fork the child process for the user session. Must be done before
    CreateSession so we can pass the child PID to logind as the session leader.
    */
    pid_t pid = fork();
    if (pid < 0) {
        perror("fork");
        return 1;
    }

    if (pid == 0) {
        /* This is the child process -- exec the compositor. */
        fprintf(stderr, "Starting child process...\n");

        /* SHORTCUT: wait for the parent process to finish CreateSession.
        Instead the daemon should signal the child when the session is ready. */
        sleep(5);

        /* TODO:
         * - set environment variables for the session
         * - privilege drop to the user account
         * - chdir to the user home directory
         * - exec the compositor in a login shell
         */

        fprintf(stderr, "Exec user session...\n");
        execlp("sleep", "sleep", "30", NULL);
        perror("execlp");  /* Error path - a successful exec does not return */
        return 1;
    }

    /* This is the parent process -- create the user session. */
    fprintf(stderr, "Started child process with PID %d\n", pid);

    sd_bus *bus = NULL;
    sd_bus_message *msg = NULL, *reply = NULL;
    sd_bus_error error = SD_BUS_ERROR_NULL;
 
    /* Connect to the system bus */
    int ret = sd_bus_open_system(&bus);
    if (ret < 0) {
        fprintf(stderr, "Failed to connect to system bus: %s\n", strerror(-ret));
        goto cleanup;
    }

    /* Construct the CreateSession message */
    ret = sd_bus_message_new_method_call(
        bus, 
        &msg, 
        "org.freedesktop.login1",         /* destination */
        "/org/freedesktop/login1",        /* object path */
        "org.freedesktop.login1.Manager", /* interface */
        "CreateSession");                 /* method */
    if (ret < 0) {
        fprintf(stderr, "CreateSession - failed to create message: %s\n", strerror(-ret));
        goto cleanup;
    }

    /* CreateSession message signature: u u s s s s s u s s b s s a(sv) */
    /* TODO: use service name "atrium" in prod, "atrium-dev" for testing */
    ret = sd_bus_message_append(msg, "uusssssussbss",
        (uint32_t)UID,  /* login user ID */
        (uint32_t)pid,  /* session leader PID */
        "atrium-dev",   /* service name */
        "wayland",      /* session type */
        "user",         /* session class */
        "",             /* desktop environment */
        SEAT,           /* seat ID */
        (uint32_t)0,    /* vtnr */
        "",             /* TTY device path */
        "",             /* X11 display device */
        0,              /* indicates remote session */
        "",             /* remote user */
        "");            /* remote host */
    if (ret < 0) {
        fprintf(stderr, "CreateSession - failed to append args: %s\n", strerror(-ret));
        goto cleanup;
    }

    /* Empty session properties array */
    ret = sd_bus_message_open_container(msg, 'a', "(sv)");
    if (ret >= 0) {
        ret = sd_bus_message_close_container(msg);
    }
    if (ret < 0) {
        fprintf(stderr, "CreateSession - failed to append args: %s\n", strerror(-ret));
        goto cleanup;
    }

    /* Call the CreateSession method */
    ret = sd_bus_call(bus, msg, 0, &error, &reply);
    if (ret < 0) {
        fprintf(stderr, "CreateSession failed: %s\n",
             error.message ? error.message : strerror(-ret));
        goto cleanup;
    }

    const char *session_id, *obj_path, *runtime_path, *ret_seat;
    int fifo_fd, existing;
    uint32_t ret_uid, ret_vtnr;
    ret = sd_bus_message_read(reply, "soshusub",
        &session_id,    /* allocated systemd session id */
        &obj_path,      /* allocated object path for the session */
        &runtime_path,  /* user runtime directory ($XDG_RUNTIME_DIR) */
        &fifo_fd,       /* fd for session lifecycle tracking */
        &ret_uid,       /* confirmed user id */
        &ret_seat,      /* confirmed seat id */
        &ret_vtnr,      /* confirmed VT number */
        &existing);     /* indicates whether session already existed */   
    if (ret < 0) {
        fprintf(stderr, "CreateSession - failed to parse reply: %s\n", strerror(-ret));
        goto cleanup;
    }

    /* Duplicate the session fd so it survives sd_bus_message_unref */
    int fifo_fd_out = fcntl(fifo_fd, F_DUPFD_CLOEXEC, 0);
    if (fifo_fd_out < 0) {
        perror("Failed to duplicate session fd");
        ret = -1;
        goto cleanup;
    }
    fprintf(stderr, "Session created: id=%s, obj_path=%s, runtime_path=%s, fifo_fd=%d, uid=%u, seat=%s, vtnr=%u\n",
        session_id, obj_path, runtime_path, fifo_fd_out, ret_uid, ret_seat, ret_vtnr);

    /* SHORTCUT: Give logind some time to activate the session. */
    sleep(2);

    /* SHORTCUT: Wait for the child process to exit. We should monitor SIGCHLD instead. */
    waitpid(pid, NULL, 0);
    fprintf(stderr, "Child process exited, closing session fd to end session...\n");
    close(fifo_fd_out);

cleanup:
    sd_bus_error_free(&error);
    sd_bus_message_unref(reply);
    sd_bus_message_unref(msg);
    sd_bus_unref(bus);
    return (ret < 0) ? -1 : 0;
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return create_session();
}
