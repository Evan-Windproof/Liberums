/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/vga_device.h>
#include <kernel/vga.h>
#include <kernel/domain.h>
#include <kernel/panic.h>

/* One legacy VGA device, no generic bus/driver policy. Grant identity uses the
 * existing domain epoch so reusing a domain slot never inherits authority. */
static struct {
    struct capability cap;
    uint32_t grant_id, grant_epoch, grant_rights;
    bool active, mapped;
} vga_device;

static bool valid_rights(uint32_t rights)
{
    return rights == RESOURCE_READ || rights == (RESOURCE_READ | RESOURCE_WRITE);
}

bool vga_device_grant(struct domain *owner, uint32_t rights)
{
    if (domain_lookup(owner->id) != owner || !valid_rights(rights)
        || vga_device.active || vga_device.grant_id != 0 || !vga_available()) {
        return false;
    }
    vga_device.grant_id = owner->id;
    vga_device.grant_epoch = owner->epoch;
    vga_device.grant_rights = rights;
    return true;
}

int32_t vga_device_info(uint32_t device, uint32_t *bytes)
{
    /* Probe was completed before paging. No user request can touch VGA ports. */
    if (device != VGA_DEVICE_TEXT || !vga_available()) {
        return RES_ENOENT;
    }
    *bytes = VGA_MAPPING_BYTES;
    return VGA_COLUMNS | VGA_ROWS << 16;
}

int32_t vga_device_acquire(struct domain *owner, uint32_t device, uint32_t rights,
                          struct resource_handle *handle)
{
    if (device != VGA_DEVICE_TEXT || !valid_rights(rights)) {
        return RES_EINVAL;
    }
    if (vga_device.grant_id != owner->id || vga_device.grant_epoch != owner->epoch
        || (rights & vga_device.grant_rights) != rights) {
        return RES_EPERM;
    }
    if (vga_device.active) {
        return RES_EBUSY;
    }
    uint32_t generation;
    if (!capability_next(vga_device.cap.generation, &generation)) {
        return RES_EOVERFLOW;
    }
    vga_device.cap = (struct capability){generation, owner->id, owner->epoch, rights};
    vga_device.active = true;
    *handle = (struct resource_handle){VGA_DEVICE_HANDLE, generation};
    return 0;
}

static int32_t resolve(struct domain *owner, struct resource_handle handle, uint32_t rights)
{
    if (handle.index != VGA_DEVICE_HANDLE || !vga_device.active) {
        return RES_ESTALE;
    }
    return capability_check(&vga_device.cap, handle.generation, owner->id, owner->epoch, rights);
}

int32_t vga_device_map(struct domain *owner, struct resource_handle handle,
                      uint32_t address, uint32_t rights)
{
    int32_t error = resolve(owner, handle, rights);
    if (error != 0) {
        return error;
    }
    if (address != VGA_USER_ADDRESS || !valid_rights(rights)) {
        return RES_EINVAL;
    }
    if (vga_device.mapped) {
        return RES_EBUSY;
    }
    error = address_space_vga_bind(&owner->space, (rights & RESOURCE_WRITE) != 0);
    if (error == 0) {
        vga_device.mapped = true;
    }
    return error;
}

int32_t vga_device_cursor(struct domain *owner, struct resource_handle handle, uint32_t cell)
{
    int32_t error = resolve(owner, handle, RESOURCE_WRITE);
    if (error != 0) { return error; }
    if (!vga_device.mapped) { return RES_ENOENT; }
    if (cell >= VGA_COLUMNS * VGA_ROWS) { return RES_EINVAL; }
    return vga_cursor_set(cell) ? 0 : -5; /* EIO: hardware readback mismatch. */
}

static void release_binding(struct domain *owner)
{
    if (vga_device.mapped && !address_space_vga_unbind(&owner->space)) {
        panic("VGA binding/PTE mismatch");
    }
    vga_device.mapped = false;
    vga_device.active = false;
    /* Keep generation for the next acquire; never clear/free device bytes. */
    vga_device.cap.owner_id = vga_device.cap.owner_epoch = vga_device.cap.rights = 0;
}

int32_t vga_device_release(struct domain *owner, struct resource_handle handle)
{
    int32_t error = resolve(owner, handle, 0);
    if (error == 0) {
        release_binding(owner);
    }
    return error;
}

void vga_device_revoke(struct domain *owner)
{
    if (vga_device.grant_id != owner->id || vga_device.grant_epoch != owner->epoch) {
        return;
    }
    if (vga_device.active) {
        release_binding(owner);
    }
    vga_device.grant_id = vga_device.grant_epoch = vga_device.grant_rights = 0;
}

bool vga_device_domain_busy(const struct domain *owner)
{
    return (vga_device.grant_id!=0 && vga_device.grant_id==owner->id && vga_device.grant_epoch==owner->epoch)
        || (vga_device.active && vga_device.cap.owner_id==owner->id && vga_device.cap.owner_epoch==owner->epoch);
}
