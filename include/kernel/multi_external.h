/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_MULTI_EXTERNAL_H
#define LIBERUMS_MULTI_EXTERNAL_H
#include <kernel/boot_info.h>
void multi_domains_run(const struct boot_context *boot);
uint32_t multi_module_devices(const char *args, uint32_t module);
#endif
