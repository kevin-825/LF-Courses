#include "syswatch.h"
#include <string.h>
#include <stdio.h>

SW_API const char *sw_strerror(int errnum, char *buf, size_t buflen) {
    if (!buf || buflen == 0) {
        return "Invalid buffer";
    }

    /* GNU-specific strerror_r returns char* */
    char *res = strerror_r(errnum, buf, buflen);

    return res;
}