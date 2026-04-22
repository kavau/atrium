#include "session.h"

/* 
 * SHORTCUT: Hardcoded session parameters
 */
#define UID  1000
#define SEAT "seat1"

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    return create_session(UID, SEAT);
}
