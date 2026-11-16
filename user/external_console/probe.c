/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Untrusted ABI adversary, not a kernel permission hook. */
#include <abi/elf.h>
#include <abi/vga.h>
#include <abi/keyboard.h>
__attribute__((section(".note.liberums"),used,aligned(4)))
static const struct liberums_abi_note requirement={9,12,LIBERUMS_NOTE_TYPE,"LIBERUMS",1,2,
    LIBERUMS_FEATURE_ELF32 | LIBERUMS_FEATURE_PREEMPTION | LIBERUMS_FEATURE_LIMITS};
volatile uint32_t probe_mode, probe_old_generation;
struct resource_handle probe_vga, probe_keyboard;
static void check(int ok,uint32_t code) { if (!ok) { liberums_exit(code); } }
static int32_t op(uint32_t n,struct resource_handle h,uint32_t va,uint32_t rights)
{
    return liberums_call(n,h.index,h.generation,va,rights,0);
}
__attribute__((section(".text.entry"))) _Noreturn void _start(void)
{
    struct resource_handle ram;
    ram.index=(uint32_t)liberums_call(SYS_PAGE_CLAIM,RESOURCE_ALL,0,0,0,&ram.generation);
    check(ram.index>0 && ram.index<=16,10);
    check(op(SYS_VGA_MAP,ram,VGA_USER_ADDRESS,3)==RES_ESTALE,11);
    check(op(SYS_KBD_POLL,ram,0,0)==RES_ESTALE,12);
    check(op(SYS_PAGE_RELEASE,ram,0,0)==0,13);
    if (probe_mode==0) {
        check(liberums_call(SYS_VGA_ACQUIRE,1,3,0,0,0)==RES_EPERM,14);
        check(liberums_call(SYS_KBD_ACQUIRE,1,0,0,0,0)==RES_EPERM,15);
        liberums_exit(0);
    }
    if (probe_mode==1) {
        check(liberums_call(SYS_VGA_ACQUIRE,1,3,0,0,0)==RES_EPERM,16);
        probe_vga.index=(uint32_t)liberums_call(SYS_VGA_ACQUIRE,1,1,0,0,&probe_vga.generation);
        check(probe_vga.index==VGA_DEVICE_HANDLE,17);
        check(op(SYS_VGA_MAP,probe_vga,VGA_USER_ADDRESS,3)==RES_EPERM,18);
        check(op(SYS_VGA_MAP,probe_vga,VGA_USER_ADDRESS,1)==0,19);
        *(volatile uint16_t *)VGA_USER_ADDRESS=0x4f58;
        liberums_exit(99);
    }
    probe_vga.index=(uint32_t)liberums_call(SYS_VGA_ACQUIRE,1,3,0,0,&probe_vga.generation);
    probe_keyboard.index=(uint32_t)liberums_call(SYS_KBD_ACQUIRE,1,0,0,0,&probe_keyboard.generation);
    check(probe_vga.index==VGA_DEVICE_HANDLE && probe_keyboard.index==KBD_HANDLE,20);
    struct resource_handle fake={VGA_DEVICE_HANDLE,0};
    check(op(SYS_VGA_MAP,fake,VGA_USER_ADDRESS,3)==RES_ESTALE,21);
    fake=(struct resource_handle){KBD_HANDLE,probe_old_generation};
    check(op(SYS_KBD_POLL,fake,0,0)==RES_ESTALE,22);
    check(op(SYS_VGA_MAP,probe_vga,0x100000,3)==RES_EINVAL,23);
    check(op(SYS_VGA_MAP,probe_vga,VGA_USER_ADDRESS,3)==0,24);
    struct resource_handle old=probe_keyboard;
    check(op(SYS_KBD_RELEASE,old,0,0)==0,25);
    probe_keyboard.index=(uint32_t)liberums_call(SYS_KBD_ACQUIRE,1,0,0,0,&probe_keyboard.generation);
    check(probe_keyboard.index==KBD_HANDLE && probe_keyboard.generation!=old.generation,26);
    check(op(SYS_KBD_POLL,old,0,0)==RES_ESTALE,27);
    check(op(SYS_KBD_POLL,probe_keyboard,0,0)==RES_EAGAIN,28);
    check(op(SYS_VGA_CURSOR,probe_vga,80,0)==0,29);
    liberums_exit(0);
}
