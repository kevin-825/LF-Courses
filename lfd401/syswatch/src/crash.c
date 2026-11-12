#include <stdio.h>
#include <errno.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include "syswatch.h"

SW_API int sw_enable_core_dumps(void) {
    struct rlimit rlim;
    char errbuf[256];

    if (getrlimit(RLIMIT_CORE, &rlim) < 0) {
        SW_LOG_INFO("Failed to get RLIMIT_CORE: %s", 
                    sw_strerror(errno, errbuf, sizeof(errbuf)));
        return -1;
    }

    SW_LOG_INFO("Previous RLIMIT_CORE: soft=%llu, hard=%llu", 
                (unsigned long long)rlim.rlim_cur, 
                (unsigned long long)rlim.rlim_max);

    /* Raise soft limit to hard ceiling (usually RLIM_INFINITY) */
    rlim.rlim_cur = rlim.rlim_max;

    if (setrlimit(RLIMIT_CORE, &rlim) < 0) {
        SW_LOG_INFO("Failed to set RLIMIT_CORE: %s", 
                    sw_strerror(errno, errbuf, sizeof(errbuf)));
        return -1;
    }

    if (prctl(PR_SET_DUMPABLE, 1L, 0L, 0L, 0L) < 0) {
        SW_LOG_INFO("Failed to set PR_SET_DUMPABLE: %s", 
                    sw_strerror(errno, errbuf, sizeof(errbuf)));
        return -1;
    }

    SW_LOG_INFO("Core dumps successfully enabled. New soft limit: %llu", 
                (unsigned long long)rlim.rlim_cur);

    return 0;
}