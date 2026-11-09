#include "syswatch.h"


int main(void)
{
    SW_LOG_INFO("%s","This is an info log message.");
    SW_LOG_WARN("%s","This is a warning log message.");
    SW_LOG_ERROR("%s","This is an error log message.");
    SW_LOG_DEBUG("%s","This is a debug log message.");

    // SW_LOG_DEBUG("%s", 5 + 3); // This will cause a warning due to type mismatch, but will not be logged if ENABLE_DEBUG_LOG is not defined.

    return 0;
}