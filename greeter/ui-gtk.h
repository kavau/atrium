#pragma once

/*
 * ui-gtk.h - GTK-based greeter UI
 *
 * Contains only GTK-specific code for the greeter UI. All toolkit-independent
 * logic should go into separate files to allow for future alternative UI
 * implementations.
 */

#include "lib/ipc.h"
#include "users.h"

#define MAX_NUM_USERS        100
#define MAX_NUM_SESSIONS     64
#define MAX_SESSION_ID_LEN   64
#define MAX_SESSION_NAME_LEN 256

typedef struct {
    char id[MAX_SESSION_ID_LEN];
    char name[MAX_SESSION_NAME_LEN];
} greeter_session;

/* Runs the greeter UI. */
int run_ui(const greeter_user *users, int num_users,
           const greeter_session *sessions, int num_sessions,
           ipc_channel *ch);
