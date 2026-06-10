#include <stdio.h>
#include "version.h"

int main(void) {
    printf("version=%s commit=%s built=%s %s\n",
           ATRIUM_VERSION, ATRIUM_COMMIT, __DATE__, __TIME__);
    return 0;
}
