/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_KERNEL_VGA_DEVICE_H
#define LIBERUMS_KERNEL_VGA_DEVICE_H
#include <abi/vga.h>
#include <stdbool.h>
struct domain;
bool vga_device_grant(struct domain *owner, uint32_t rights);
int32_t vga_device_info(uint32_t device, uint32_t *bytes);
int32_t vga_device_acquire(struct domain *owner, uint32_t device, uint32_t rights,
                          struct resource_handle *handle);
int32_t vga_device_map(struct domain *owner, struct resource_handle handle,
                      uint32_t address, uint32_t rights);
int32_t vga_device_cursor(struct domain *owner, struct resource_handle handle, uint32_t cell);
int32_t vga_device_release(struct domain *owner, struct resource_handle handle);
/* Trusted revocation also removes the boot grant, so reacquisition is denied. */
void vga_device_revoke(struct domain *owner);
#endif
