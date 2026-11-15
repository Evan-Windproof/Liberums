/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/capability.h>
bool capability_next(uint32_t current, uint32_t *next)
{
    if (current == UINT32_MAX) {
        return false; /* Permanently retire the slot rather than resurrect old handles. */
    }
    *next = current + 1;
    return true;
}
bool capability_rights_valid(uint32_t rights)
{
    return (rights & RESOURCE_READ) != 0 && (rights & ~RESOURCE_ALL) == 0;
}
int32_t capability_check(const struct capability *cap, uint32_t generation,
                         uint32_t caller, uint32_t epoch, uint32_t required)
{
    if (generation == 0 || generation != cap->generation) {
        return RES_ESTALE;
    }
    if (caller != cap->owner_id || epoch != cap->owner_epoch
        || (required & cap->rights) != required) {
        return RES_EPERM;
    }
    return 0;
}
