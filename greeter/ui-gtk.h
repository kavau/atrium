/*
 * ui-gtk.h - GTK-based greeter UI
 *
 * Contains only GTK-specific code for the greeter UI. All toolkit-independent
 * logic should go into separate files to allow for future alternative UI
 * implementations.
 */

#pragma once

#include "lib/ipc.h"
#include "users.h"

#define MAX_NUM_USERS 100

/* Runs the greeter UI. */
int run_ui(const greeter_user *users, int num_users, ipc_channel *ch);
