#include "syswatch_plugin.h"
#include <stdio.h>
#include <stdlib.h>

static int dummy_cpu_init(void) {
    /* Perform plugin-specific setup/initialization */
    return 0;
}

static int dummy_cpu_collect(void) {
    /* Simulate collecting a CPU usage metric */
    int simulated_usage = rand() % 100;
    return simulated_usage;
}

/* Exported plugin entry struct marked with SW_API so it stays visible */
SW_API struct sw_metric_plugin sw_plugin_entry = {
    .name = "dummy_cpu_collector",
    .init = dummy_cpu_init,
    .collect = dummy_cpu_collect
};