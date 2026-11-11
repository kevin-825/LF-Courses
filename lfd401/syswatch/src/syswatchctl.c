#include "syswatch.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char *argv[]) {
    (void)argc;
    (void)argv;

    SW_LOG_INFO("syswatchctl CLI client initialized.");

    /* Demonstrate error reporting using libsyswatch's sw_strerror() */
    int fd = open("/nonexistent_syswatch_socket", O_RDWR);
    if (fd < 0) {
        char errbuf[256];
        const char *msg = sw_strerror(errno, errbuf, sizeof(errbuf));
        SW_LOG_INFO("Failed to connect to daemon socket: %s (errno=%d)", msg, errno);
    } else {
        close(fd);
    }

    return EXIT_SUCCESS;
}