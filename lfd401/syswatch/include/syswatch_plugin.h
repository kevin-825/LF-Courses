#ifndef SYSWATCH_PLUGIN_H
#define SYSWATCH_PLUGIN_H

#include "syswatch.h"

/* Structural contract for metric plugins */
struct sw_metric_plugin {
    const char *name;
    int (*init)(void);
    int (*collect)(void);
};

/* The standard symbol name that syswatchd looks up via dlsym() */
#define SW_PLUGIN_SYMBOL_NAME "sw_plugin_entry"


#endif /* SYSWATCH_PLUGIN_H */