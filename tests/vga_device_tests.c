/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Boundary tests for the real authority state machine. Paging is deliberately
 * mocked here; tests/phase71.py independently inspects real QEMU page tables. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <kernel/domain.h>
#include <kernel/vga_device.h>
static struct domain owners[2];
static int32_t bind_result;
static uint32_t binds, unbinds;
static bool hardware = true;
struct domain *domain_lookup(uint32_t id) { return id >= 1 && id <= 2 ? &owners[id-1] : NULL; }
bool vga_cursor_set(uint32_t cell) { return cell < 2000; }
bool vga_available(void) { return hardware; }
int32_t address_space_vga_bind(struct address_space *space, bool writable)
{
    (void)space; (void)writable;
    ++binds;
    return bind_result;
}
bool address_space_vga_unbind(struct address_space *space)
{
    (void)space;
    ++unbinds;
    return true;
}
_Noreturn void panic(const char *message) { puts(message); abort(); }
#include "../kernel/vga_device.c"
int main(void)
{
    struct domain *a=&owners[0], *b=&owners[1];
    a->id=1; a->epoch=1; b->id=2; b->epoch=1;
    uint32_t bytes=0;
    struct resource_handle h, newer, ram={1,1};
    assert(vga_device_info(2,&bytes)==RES_ENOENT);
    hardware=false;
    assert(vga_device_info(1,&bytes)==RES_ENOENT);
    assert(!vga_device_grant(a,3));
    hardware=true;
    assert(vga_device_info(1,&bytes)==(int32_t)(80|25<<16) && bytes==4096);
    assert(!vga_device_grant(a,RESOURCE_ALL));
    assert(vga_device_grant(a,1));
    assert(!vga_device_grant(b,3));
    assert(vga_device_acquire(a,2,1,&h)==RES_EINVAL);
    assert(vga_device_acquire(a,1,0,&h)==RES_EINVAL);
    assert(vga_device_acquire(a,1,3,&h)==RES_EPERM); /* grant attenuation */
    assert(vga_device_acquire(b,1,1,&h)==RES_EPERM);
    assert(vga_device_acquire(a,1,1,&h)==0);
    assert(vga_device_acquire(a,1,1,&newer)==RES_EBUSY);
    assert(vga_device_map(b,h,VGA_USER_ADDRESS,1)==RES_EPERM);
    assert(vga_device_release(b,h)==RES_EPERM);
    assert(vga_device_map(a,ram,VGA_USER_ADDRESS,1)==RES_ESTALE);
    ++a->epoch;
    assert(vga_device_map(a,h,VGA_USER_ADDRESS,1)==RES_EPERM);
    assert(vga_device_acquire(a,1,1,&newer)==RES_EPERM);
    --a->epoch;
    assert(vga_device_map(a,h,VGA_USER_ADDRESS,3)==RES_EPERM);
    assert(vga_device_map(a,h,0x100000,1)==RES_EINVAL);
    assert(vga_device_map(a,h,VGA_USER_ADDRESS+1,1)==RES_EINVAL);
    assert(vga_device_map(a,h,VGA_USER_ADDRESS+4096,1)==RES_EINVAL);
    assert(binds==0);
    bind_result=RES_ENOMEM;
    assert(vga_device_map(a,h,VGA_USER_ADDRESS,1)==RES_ENOMEM);
    assert(!vga_device.mapped && vga_device.active); /* rollback, retryable */
    bind_result=0;
    assert(vga_device_map(a,h,VGA_USER_ADDRESS,1)==0);
    assert(vga_device_cursor(a,h,0)==RES_EPERM);
    assert(vga_device_map(a,h,VGA_USER_ADDRESS,1)==RES_EBUSY);
    assert(vga_device_release(a,h)==0 && unbinds==1);
    assert(vga_device_release(a,h)==RES_ESTALE);
    assert(vga_device_acquire(a,1,1,&newer)==0 && newer.generation==h.generation+1);
    assert(vga_device_map(a,h,VGA_USER_ADDRESS,1)==RES_ESTALE);
    vga_device_revoke(a);
    vga_device_revoke(a); /* idempotent, never double free */
    assert(unbinds==1 && vga_device.grant_id==0);
    assert(vga_device_acquire(a,1,1,&h)==RES_EPERM);
    assert(vga_device_grant(b,3));
    vga_device.cap.generation=UINT32_MAX-1;
    assert(vga_device_acquire(b,1,3,&h)==0 && h.generation==UINT32_MAX);
    assert(vga_device_map(b,h,VGA_USER_ADDRESS,3)==0);
    assert(vga_device_cursor(b,h,1999)==0);
    assert(vga_device_cursor(b,h,2000)==RES_EINVAL);
    assert(vga_device_cursor(a,h,0)==RES_EPERM);
    vga_device_revoke(b);
    assert(unbinds==2);
    assert(vga_device_grant(b,3));
    assert(vga_device_acquire(b,1,3,&h)==RES_EOVERFLOW);
    assert(!vga_device.active); /* generation permanently exhausted */
    puts("PASS: VGA authority: grant attenuation, epoch, namespace, exclusive owner, rights, bounds, OOM rollback, stale handles, revoke, generation exhaustion");
    return 0;
}
