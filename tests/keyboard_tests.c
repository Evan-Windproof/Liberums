/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Real queue/authority code, mock only hardware. QEMU separately proves IRQ1. */
#include <assert.h>
#include <stdio.h>
#include <kernel/domain.h>
#include <arch/x86/ps2.h>
static struct domain owners[2];
static uint8_t input[256], mask=0xfe;
static uint32_t offset, length, eo, stops;
static bool start_ok=true, bad;
struct domain *domain_lookup(uint32_t id) { return id>=1 && id<=2 ? &owners[id-1] : NULL; }
bool timer_enabled(void) { return true; }
bool ps2_start(void) { return start_ok; }
void ps2_stop(void) { ++stops; }
void pic_keyboard_mask(bool set) { mask=set ? mask|2u : mask&~2u; }
void pic_keyboard_eoi(void) { ++eo; }
enum ps2_result ps2_read(uint8_t *byte)
{
    if (offset==length) { return PS2_EMPTY; }
    *byte=input[offset++];
    return bad ? PS2_DISCARD : PS2_DATA;
}
void serial_write(const char *s) { (void)s; }
#include "../kernel/keyboard.c"
static void feed(uint32_t count)
{
    offset=0; length=count;
    for (uint32_t i=0;i<count;++i) { input[i]=(uint8_t)(i%127+1); }
    do {
        uint32_t before=offset, e=eo;
        keyboard_dispatch(NULL);
        assert(offset-before<=32 && eo==e+1);
    } while(offset!=length);
}
int main(void)
{
    struct domain *a=&owners[0], *b=&owners[1];
    a->id=1; a->epoch=1; b->id=2; b->epoch=1;
    uint32_t value=0;
    struct resource_handle h,newer;
    start_ok=false; assert(!keyboard_start() && mask==0xfe);
    start_ok=true; assert(keyboard_start() && mask==0xfc);
    feed(5); assert(keyboard.count==0); /* unowned bytes never retained */
    assert(keyboard_info(2,&value)==RES_ENOENT);
    assert(keyboard_info(1,&value)==64 && value==1);
    assert(keyboard_grant(a) && !keyboard_grant(b));
    assert(keyboard_acquire(b,1,&h)==RES_EPERM);
    assert(keyboard_acquire(a,2,&h)==RES_EINVAL);
    assert(keyboard_acquire(a,1,&h)==0);
    assert(keyboard_acquire(a,1,&newer)==RES_EBUSY);
    assert(keyboard_poll(b,h,&value)==RES_EPERM);
    assert(keyboard_release(b,h)==RES_EPERM);
    assert(keyboard_stats(b,h,&value)==RES_EPERM);
    ++a->epoch; assert(keyboard_poll(a,h,&value)==RES_EPERM); --a->epoch;
    assert(keyboard_poll(a,(struct resource_handle){1,h.generation},&value)==RES_ESTALE);
    assert(keyboard_poll(a,h,&value)==RES_EAGAIN);
    feed(64);
    for (uint32_t i=1;i<=32;++i) { assert(keyboard_poll(a,h,&value)==0 && value==i); }
    feed(32); /* wrap around, FIFO retained */
    for (uint32_t i=33;i<=64;++i) { assert(keyboard_poll(a,h,&value)==0 && value==i); }
    for (uint32_t i=1;i<=32;++i) { assert(keyboard_poll(a,h,&value)==0 && value==i); }
    feed(80);
    assert(keyboard.count==64 && keyboard.overflow && keyboard.dropped==16);
    assert(keyboard_poll(a,h,&value)==RES_EOVERFLOW && value==80);
    assert(keyboard.count==0 && !keyboard.overflow && keyboard.lost==80);
    assert(keyboard_poll(a,h,&value)==RES_EAGAIN);
    bad=true; feed(1); bad=false;
    assert(keyboard_poll(a,h,&value)==RES_EOVERFLOW && value==1);
    keyboard.dropped=UINT32_MAX; keyboard.lost=UINT32_MAX; keyboard.overflow=true;
    feed(1); assert(keyboard_poll(a,h,&value)==RES_EOVERFLOW && value==UINT32_MAX);
    feed(2); assert(keyboard_release(a,h)==0);
    assert(keyboard.count==0 && keyboard_poll(a,h,&value)==RES_ESTALE);
    assert(keyboard_acquire(a,1,&newer)==0 && newer.generation==h.generation+1 && keyboard.lost==0);
    assert(keyboard_poll(a,h,&value)==RES_ESTALE);
    keyboard_revoke(a); keyboard_revoke(a);
    assert(mask==0xfe && stops==1 && keyboard.count==0 && !keyboard.active);
    assert(keyboard_acquire(a,1,&h)==RES_EPERM);
    assert(keyboard_start() && keyboard_grant(b));
    keyboard.cap.generation=UINT32_MAX-1;
    assert(keyboard_acquire(b,1,&h)==0 && h.generation==UINT32_MAX);
    assert(keyboard_release(b,h)==0);
    assert(keyboard_acquire(b,1,&h)==RES_EOVERFLOW && !keyboard.active);
    puts("PASS: keyboard bounded drain/FIFO/wrap, unowned drop, overflow reset/saturation, authority/epoch/namespace, release/revoke/generation, IRQ0 mask preservation");
    return 0;
}
