#pragma once

/*
 * seat.h - seat management
 */

/* TODO: avoid reaching into session module by storing the pam_handle directly. */
#include "daemon/session/auth.h"

#define MAX_LEN_SEAT 64

/* Holds the current state of a seat */
typedef struct seat {
    char name[MAX_LEN_SEAT];
    int vtnr;               /* allocated VT number; 0 for non-seat0 seats */
    auth_result pam_result; /* PAM session state for the seat */
} seat;
