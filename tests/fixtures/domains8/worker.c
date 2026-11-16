/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Untrusted ABI-only resource/scheduling adversary. Test inputs are private BSS. */
#include <abi/elf.h>
#include <abi/vga.h>
#include <abi/keyboard.h>
__attribute__((section(".note.liberums"),used,aligned(4)))
static const struct liberums_abi_note requirement={9,12,LIBERUMS_NOTE_TYPE,"LIBERUMS",1,2,
    LIBERUMS_FEATURE_PAGES|LIBERUMS_FEATURE_ELF32|LIBERUMS_FEATURE_PREEMPTION|LIBERUMS_FEATURE_REVOCATION};
#ifndef WORKER_MARKER
#define WORKER_MARKER 0xa8000000u
#endif
volatile uint32_t worker_marker=WORKER_MARKER;
volatile uint32_t worker_command=1;
struct worker_control {
    uint32_t stage,sequence,target,address,expected_error,want_devices,raw_count,raw;
    uint32_t old_request,received_pfn;
    struct resource_handle handles[4],attack,old_vga,old_keyboard,vga,keyboard;
};
volatile struct worker_control worker;
void worker_spin(void);
static void expect(int condition,uint32_t code) { if (!condition) { liberums_exit(code); } }
static int32_t op(uint32_t n,struct resource_handle h,uint32_t va,uint32_t rights)
{
    return liberums_call(n,h.index,h.generation,va,rights,0);
}
__attribute__((noinline)) void worker_checkpoint(uint32_t stage)
{
    worker.stage=stage;++worker.sequence;
    __asm__ volatile (".global worker_observed; worker_observed: nop" : : : "memory");
}
__attribute__((section(".text.entry"))) _Noreturn void _start(void)
{
    uint32_t bindings;
    expect(liberums_call(SYS_RESOURCE_LIMITS,0,0,0,0,&bindings)==4 && bindings==4,10);
    if (worker.attack.index) { expect(op(SYS_PAGE_QUERY,worker.attack,0,0)==RES_ESTALE,11); }
    if (worker.old_request) {
        expect(liberums_call(SYS_REVOKE_ACK,worker.attack.index,worker.attack.generation,worker.old_request,0,0)==RES_ESTALE,12);
    }
    for (uint32_t i=0;i<4;++i) {
        uint32_t generation;
        int32_t index=liberums_call(SYS_PAGE_CLAIM,RESOURCE_ALL,0,0,0,&generation);
        expect(index>0 && index<=16,13);
        struct resource_handle h={(uint32_t)index,generation};worker.handles[i]=h;
        expect(op(SYS_PAGE_MAP,h,RESOURCE_WINDOW+i*4096,3)==0,14);
        volatile uint32_t *page=(void *)(uintptr_t)(RESOURCE_WINDOW+i*4096);
        for (uint32_t j=0;j<1024;++j) { expect(page[j]==0,15);page[j]=worker_marker^i^j; }
    }
    expect(liberums_call(SYS_PAGE_CLAIM,RESOURCE_ALL,0,0,0,0)==RES_ENOSPC,16);
    if (worker.attack.index) { expect(op(SYS_PAGE_QUERY,worker.attack,0,0)==RES_ESTALE,17); }
    if (worker.want_devices==1) {
        uint32_t generation;
        int32_t index=liberums_call(SYS_VGA_ACQUIRE,1,3,0,0,&generation);
        expect(index==(int32_t)VGA_DEVICE_HANDLE,18);worker.vga=(struct resource_handle){(uint32_t)index,generation};
        index=liberums_call(SYS_KBD_ACQUIRE,1,0,0,0,&generation);
        expect(index==(int32_t)KBD_HANDLE,19);worker.keyboard=(struct resource_handle){(uint32_t)index,generation};
        expect(op(SYS_VGA_MAP,worker.vga,VGA_USER_ADDRESS,3)==0,20);
        *(volatile uint16_t *)VGA_USER_ADDRESS=0x1f38;
        if (worker.old_vga.index) { expect(op(SYS_VGA_MAP,worker.old_vga,VGA_USER_ADDRESS,3)==RES_ESTALE,21); }
        if (worker.old_keyboard.index) { expect(op(SYS_KBD_POLL,worker.old_keyboard,0,0)==RES_ESTALE,22); }
    } else {
        expect(liberums_call(SYS_VGA_ACQUIRE,1,3,0,0,0)==RES_EPERM,23);
        expect(liberums_call(SYS_KBD_ACQUIRE,1,0,0,0,0)==RES_EPERM,24);
    }
    worker_checkpoint(1);
    liberums_call(SYS_YIELD,0,0,0,0,0);
    for (;;) {
        uint32_t command=worker_command;
        if (command==0) {
            __asm__ volatile (".global worker_idle; worker_idle: nop" : : : "memory");
            liberums_call(SYS_YIELD,0,0,0,0,0);continue;
        }
        if (command==1) { liberums_exit(0); }
        if (command==2) { __asm__ volatile ("ud2"); }
        if (command==3) { worker_spin();continue; }
        worker_command=0;
        if (command==4) { (void)*(volatile uint32_t *)(uintptr_t)worker.address; liberums_exit(99); }
        if (command==5 || command==9 || command==14) {
            int32_t result=op(SYS_PAGE_TRANSFER,worker.handles[0],worker.target,RESOURCE_ALL);
            expect(result==(int32_t)worker.expected_error,25);
            expect(op(SYS_PAGE_QUERY,worker.handles[0],0,0)>0,26);
            worker_checkpoint(2);
        } else if (command==6) {
            expect(op(SYS_PAGE_RELEASE,worker.handles[0],0,0)==0,27);
            worker.handles[0]=(struct resource_handle){0,0};worker_checkpoint(3);
        } else if (command==7) {
            struct resource_handle old=worker.handles[0];
            expect(op(SYS_PAGE_TRANSFER,old,worker.target,RESOURCE_ALL)==0,28);
            expect(op(SYS_PAGE_QUERY,old,0,0)==RES_ESTALE,29);
            worker.handles[0]=(struct resource_handle){0,0};worker_checkpoint(4);
        } else if (command==8) {
            uint32_t generation;
            int32_t index=liberums_call(SYS_PAGE_RECEIVE,0,0,0,0,&generation);
            expect(index>0,30);struct resource_handle h={(uint32_t)index,generation};worker.handles[0]=h;
            expect(op(SYS_PAGE_MAP,h,RESOURCE_WINDOW,3)==0,31);
            volatile uint32_t *page=(void *)RESOURCE_WINDOW;
            for (uint32_t j=0;j<1024;++j) { expect(page[j]==0,32);page[j]=worker_marker^j; }
            worker.received_pfn=(uint32_t)op(SYS_PAGE_QUERY,h,0,0);worker_checkpoint(5);
        } else if (command==10) {
            expect(op(SYS_PAGE_QUERY,worker.attack,0,0)==(int32_t)worker.expected_error,33);
            expect(op(SYS_PAGE_MAP,worker.attack,RESOURCE_WINDOW+5*4096,1)==(int32_t)worker.expected_error,34);
            worker_checkpoint(6);
        } else if (command==11) {
            uint32_t raw;int32_t result=liberums_call(SYS_KBD_POLL,worker.keyboard.index,worker.keyboard.generation,0,0,&raw);
            if (result==RES_EAGAIN) { worker_command=11;liberums_call(SYS_YIELD,0,0,0,0,0);continue; }
            expect(result==0,35);worker.raw=raw;++worker.raw_count;worker_checkpoint(7);
        } else if (command==12) {
            expect(op(SYS_PAGE_RELEASE,worker.handles[1],0,0)==0,36);
            worker.handles[1]=(struct resource_handle){0,0};worker_checkpoint(8);
        } else if (command==13) { (void)*(volatile uint32_t *)RESOURCE_WINDOW;liberums_exit(98); }
        else { liberums_exit(97); }
    }
}
