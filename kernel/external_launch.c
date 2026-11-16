/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/external_launch.h>
#include <kernel/device_grants.h>
#include <kernel/boot_info.h>
struct external_config external_config_read(const char *args)
{
    bool console = boot_has_option(args, "external-console");
    struct external_config config = {console || boot_has_option(args, "external"), false, 2, 0};
    if (!config.selected) { return config; }
    if (console || boot_has_option(args, "external-vga")) { config.devices |= DEVICE_VGA_READ | DEVICE_VGA_WRITE; }
    if (boot_has_option(args, "external-vga-readonly")) {
        config.devices = (config.devices | DEVICE_VGA_READ) & ~DEVICE_VGA_WRITE;
    }
    if (console || boot_has_option(args, "external-keyboard")) { config.devices |= DEVICE_KEYBOARD; }
    config.preempt = console || (config.devices & DEVICE_KEYBOARD) != 0;
    if (console || boot_has_option(args, "external-once")) { config.lifetimes = 1; }
    if (boot_has_option(args, "external-repeat")) { config.lifetimes = 2; }
    return config;
}
