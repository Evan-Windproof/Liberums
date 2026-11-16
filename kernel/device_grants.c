/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/device_grants.h>
#include <kernel/vga_device.h>
#include <kernel/keyboard.h>
#include <kernel/vga.h>

int32_t device_grants_apply(struct domain *owner, uint32_t requested)
{
    if ((requested & ~7u) != 0 || ((requested & DEVICE_VGA_WRITE) && !(requested & DEVICE_VGA_READ))) {
        return RES_EINVAL;
    }
    bool vga_granted = false;
    if (requested & DEVICE_VGA_READ) {
        if (!vga_available()) { return RES_ENOENT; }
        uint32_t rights = RESOURCE_READ | ((requested & DEVICE_VGA_WRITE) ? RESOURCE_WRITE : 0);
        if (!vga_device_grant(owner, rights)) { return RES_EBUSY; }
        vga_granted = true;
    }
    if (requested & DEVICE_KEYBOARD) {
        if (!keyboard_start()) {
            if (vga_granted) { vga_device_revoke(owner); }
            return -5; /* EIO or unavailable exclusive keyboard; no new grant survives. */
        }
        if (!keyboard_grant(owner)) {
            keyboard_abort_start();
            if (vga_granted) { vga_device_revoke(owner); }
            return RES_EBUSY;
        }
    }
    return 0;
}
void device_grants_revoke(struct domain *owner, uint32_t requested)
{
    if (requested & DEVICE_KEYBOARD) { keyboard_revoke(owner); }
    if (requested & (DEVICE_VGA_READ | DEVICE_VGA_WRITE)) { vga_device_revoke(owner); }
}
