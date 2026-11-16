/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/external_launch.h>
#include <kernel/device_grants.h>
#include <kernel/boot_info.h>
#include <kernel/multi_external.h>
struct external_config external_config_read(const char *args)
{
    bool console = boot_has_option(args, "external-console");
    bool multi = boot_has_option(args,"multi-external");
    struct external_config config = {multi || console || boot_has_option(args, "external"), false, 2, 0, multi};
    if (!config.selected) { return config; }
    if (multi) {
        config.preempt=true; config.lifetimes=1;
        for (uint32_t module=1;module<=BOOT_MODULE_MAX;++module) { config.devices |= multi_module_devices(args,module); }
        return config;
    }
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
