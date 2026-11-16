/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_EXTERNAL_LAUNCH_H
#define LIBERUMS_EXTERNAL_LAUNCH_H
#include <stdbool.h>
#include <stdint.h>
struct external_config {
    bool selected, preempt;
    uint32_t lifetimes, devices;
    bool multi;
};
/* Command line is trusted experiment configuration, not Secure Boot. */
struct external_config external_config_read(const char *args);
#endif
