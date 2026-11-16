/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <kernel/device_grants.h>
#include <kernel/keyboard.h>
#include <kernel/vga_device.h>
#include <kernel/domain.h>
#include <arch/x86/ps2.h>
static struct domain owners[2];
static bool video=true, ps2=true;
static uint8_t mask=0xfe;
static uint32_t mapped, unmapped, stopped;
struct domain *domain_lookup(uint32_t id) { return id>=1 && id<=2 ? &owners[id-1] : NULL; }
bool timer_enabled(void) { return true; }
bool vga_available(void) { return video; }
bool vga_cursor_set(uint32_t cell) { return cell<2000; }
bool ps2_start(void) { return ps2; }
void ps2_stop(void) { ++stopped; }
enum ps2_result ps2_read(uint8_t *byte) { (void)byte; return PS2_EMPTY; }
void pic_keyboard_mask(bool set) { mask=set ? mask|2u : mask&~2u; }
void pic_keyboard_eoi(void) { }
void serial_write(const char *s) { (void)s; }
int32_t address_space_vga_bind(struct address_space *s,bool w) { (void)s;(void)w;++mapped;return 0; }
bool address_space_vga_unbind(struct address_space *s) { (void)s;++unmapped;return true; }
_Noreturn void panic(const char *s) { puts(s);abort(); }
int main(void)
{
    struct domain *a=&owners[0],*b=&owners[1];
    a->id=1;a->epoch=1;b->id=2;b->epoch=1;
    struct resource_handle v,k,old;
    uint32_t value;
    assert(device_grants_apply(a,8)==RES_EINVAL);
    assert(device_grants_apply(a,2)==RES_EINVAL);
    assert(device_grants_apply(a,0)==0);
    video=false;assert(device_grants_apply(a,7)==RES_ENOENT);video=true;
    ps2=false;assert(device_grants_apply(a,7)==-5);ps2=true;
    assert(vga_device_acquire(a,1,3,&v)==RES_EPERM); /* VGA rollback on PS/2 failure */
    struct domain invalid={0}; invalid.id=9;
    assert(device_grants_apply(&invalid,4)==RES_EBUSY && mask==0xfe);
    assert(device_grants_apply(b,1)==0);
    assert(device_grants_apply(a,7)==RES_EBUSY);
    assert(vga_device_acquire(b,1,1,&v)==0 && vga_device_release(b,v)==0);
    device_grants_revoke(b,1);
    assert(device_grants_apply(b,4)==0);
    assert(device_grants_apply(a,7)==-5);
    assert(keyboard_acquire(b,1,&k)==0); /* unrelated old grant survives failed bundle */
    device_grants_revoke(b,4);
    assert(device_grants_apply(a,7)==0);
    assert(vga_device_acquire(a,1,3,&v)==0 && keyboard_acquire(a,1,&k)==0);
    assert(vga_device_map(a,v,VGA_USER_ADDRESS,3)==0);
    assert(keyboard_poll(b,k,&value)==RES_EPERM);
    assert(vga_device_map(b,v,VGA_USER_ADDRESS,1)==RES_EPERM);
    device_grants_revoke(a,4);
    assert(keyboard_poll(a,k,&value)==RES_ESTALE && mask==0xfe);
    assert(vga_device_cursor(a,v,17)==0 && unmapped==0); /* VGA independent */
    device_grants_revoke(a,1);
    assert(unmapped==1 && vga_device_cursor(a,v,17)==RES_ESTALE);
    assert(device_grants_apply(a,7)==0);
    old=k;assert(keyboard_acquire(a,1,&k)==0 && k.generation>old.generation);
    assert(keyboard_poll(a,old,&value)==RES_ESTALE);
    assert(vga_device_acquire(a,1,3,&v)==0 && vga_device_map(a,v,VGA_USER_ADDRESS,3)==0);
    device_grants_revoke(a,1);
    assert(keyboard_poll(a,k,&value)==RES_EAGAIN && mask==0xfc); /* keyboard independent */
    ++a->epoch;
    assert(keyboard_poll(a,k,&value)==RES_EPERM && keyboard_acquire(a,1,&old)==RES_EPERM);
    --a->epoch;device_grants_revoke(a,4);
    assert(device_grants_apply(a,1)==0);
    assert(vga_device_acquire(a,1,3,&v)==RES_EPERM);
    assert(vga_device_acquire(a,1,1,&v)==0);
    assert(vga_device_map(a,v,VGA_USER_ADDRESS,3)==RES_EPERM);
    device_grants_revoke(a,7);
    assert(mapped==2 && unmapped==2 && stopped>=3);
    puts("PASS: trusted device bundle rollback, unrelated owner preservation, independent revoke, readonly/epoch/generation/peer isolation");
}
