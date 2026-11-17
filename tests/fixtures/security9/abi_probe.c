/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <abi/elf.h>
#include <abi/keyboard.h>
__attribute__((section(".note.liberums"),used,aligned(4)))
static const struct liberums_abi_note note={9,12,LIBERUMS_NOTE_TYPE,"LIBERUMS",1,2,0};
volatile int32_t probe_result;
volatile uint32_t probe_extra;
__attribute__((section(".text.entry"))) _Noreturn void _start(void)
{
    uint32_t extra=0;
    probe_result=liberums_call(SYS_KBD_RELEASE,KBD_HANDLE,1,0xcafe1234,0,&extra);
    probe_extra=extra;
    const uint32_t unknown[]={5,15,27,31,37,39,45,0xffffffffu};
    for (unsigned i=0;i<sizeof(unknown)/sizeof(unknown[0]);++i) {
        if (liberums_call(unknown[i],0,0,0,0,0)!=-38) { liberums_exit(80); }
    }
    __asm__ volatile(".global abi_probe_done; abi_probe_done: nop" : : : "memory");
    liberums_exit(probe_result==RES_ESTALE && probe_extra==0 ? 0 : 79);
}
