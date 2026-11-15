/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/capability.h>
#include <assert.h>
#include <stdio.h>
int main(void)
{
    uint32_t next = 0;
    assert(capability_next(0, &next) && next == 1);
    assert(capability_next(UINT32_MAX - 1, &next) && next == UINT32_MAX);
    assert(!capability_next(UINT32_MAX, &next) && next == UINT32_MAX);
    struct capability cap = {UINT32_MAX, 1, 7, RESOURCE_READ | RESOURCE_MAP};
    assert(capability_check(&cap, UINT32_MAX, 1, 7, RESOURCE_MAP) == 0);
    assert(capability_check(&cap, 0, 1, 7, 0) == RES_ESTALE);
    assert(capability_check(&cap, UINT32_MAX - 1, 1, 7, 0) == RES_ESTALE);
    assert(capability_check(&cap, UINT32_MAX, 2, 7, 0) == RES_EPERM);
    assert(capability_check(&cap, UINT32_MAX, 1, 8, 0) == RES_EPERM);
    assert(capability_check(&cap, UINT32_MAX, 1, 7, RESOURCE_WRITE) == RES_EPERM);
    assert(capability_check(&cap, UINT32_MAX, 1, 7, 64) == RES_EPERM);
    assert(capability_rights_valid(RESOURCE_ALL));
    assert(capability_rights_valid(RESOURCE_READ));
    assert(!capability_rights_valid(RESOURCE_WRITE));
    assert(!capability_rights_valid(0));
    assert(!capability_rights_valid(RESOURCE_READ | 64));
    puts("PASS: 15 capability assertions: full generation boundary, epoch, owner and rights");
    return 0;
}
