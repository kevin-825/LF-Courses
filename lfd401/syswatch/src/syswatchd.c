#include "syswatch.h"
#include "syswatch_plugin.h"
#include <stdio.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <string.h>

static void trigger_test_crash(const char *plugin_name) {
    SW_LOG_INFO("Triggering test crash for plugin: %s", plugin_name ? plugin_name : "none");
    
    /* Create local struct to verify stack frame in core dump */
    struct {
        const char *name;
        int status_code;
    } test_ctx = { .name = plugin_name, .status_code = 0xdead };

    (void)test_ctx; /* Avoid unused variable compiler warning */

    /* Intentionally dereference bad memory location */
    volatile int *bad_ptr = (volatile int *)0xdead0000;
    *bad_ptr = 42;
}

int main(int argc, char *argv[]) {
        /* 1. Enable core dumps on startup */
    if (sw_enable_core_dumps() != 0) {
        fprintf(stderr, "Warning: Failed to enable core dumps\n");
    }

    /* 2. Check for --crash-test flag */
    if (argc > 1 && strcmp(argv[1], "--crash-test") == 0) {
        trigger_test_crash("test_plugin");
    }

    const char *plugin_path = (argc > 1) ? argv[1] : "./build/lib/plugin_dummy_cpu.so";
    //char errbuf[256];

    SW_LOG_INFO("syswatchd daemon starting up (Milestone 2)...");

    /* 1. Dynamically load the plugin shared object */
    void *handle = dlopen(plugin_path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        SW_LOG_INFO("Failed to load plugin '%s': %s", plugin_path, dlerror());
        return EXIT_FAILURE;
    }

    /* 2. Clear any existing dynamic loading error */
    dlerror();

    /* 3. Look up the standard plugin entry symbol safely */
    struct sw_metric_plugin *plugin = NULL;
    *(void **)(&plugin) = dlsym(handle, SW_PLUGIN_SYMBOL_NAME);

    const char *dlsym_err = dlerror();
    if (dlsym_err != NULL) {
        SW_LOG_INFO("dlsym failed for symbol '%s': %s", SW_PLUGIN_SYMBOL_NAME, dlsym_err);
        dlclose(handle);
        return EXIT_FAILURE;
    }

    /* 4. Initialize and execute the plugin */
    SW_LOG_INFO("Loaded metric plugin: %s", plugin->name);

    if (plugin->init() == 0) {
        int metric_val = plugin->collect();
        SW_LOG_INFO("Plugin [%s] collected CPU metric: %d%%", plugin->name, metric_val);
    } else {
        SW_LOG_INFO("Plugin [%s] initialization failed!", plugin->name);
    }

    /* 5. Unload the plugin shared library */
    dlclose(handle);
    SW_LOG_INFO("syswatchd daemon exiting cleanly.");

    return EXIT_SUCCESS;
}