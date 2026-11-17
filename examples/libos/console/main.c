/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "runtime.h"
#include "core.h"
struct console console_state;
int libos_main(void)
{
    uint32_t extra;
    if (liberums_call(SYS_VGA_INFO,1,0,0,0,&extra)<0
        || liberums_call(SYS_KBD_INFO,1,0,0,0,&extra)<0) { return 76; }
    int32_t index=liberums_call(SYS_VGA_ACQUIRE,1,3,0,0,&console_state.vga.generation);
    if (index<0) { return 77; } /* INFO succeeded, but grant is still required. */
    console_state.vga.index=(uint32_t)index;
    index=liberums_call(SYS_KBD_ACQUIRE,1,0,0,0,&console_state.keyboard.generation);
    if (index<0) {
        liberums_call(SYS_VGA_RELEASE,console_state.vga.index,console_state.vga.generation,0,0,0);
        return 78;
    }
    console_state.keyboard.index=(uint32_t)index;
    if (liberums_call(SYS_VGA_MAP,console_state.vga.index,console_state.vga.generation,VGA_USER_ADDRESS,3,0)) { return 79; }
    console_start(&console_state,"Independent teaching LibOS\nType help; exit releases both devices\n",15,1);
    example_observe(1,0);
    while (console_state.running) {
        uint32_t raw=0;
        int32_t result=liberums_call(SYS_KBD_POLL,console_state.keyboard.index,console_state.keyboard.generation,0,0,&raw);
        if (result==RES_EAGAIN) { libos_yield();continue; }
        if (result==RES_EOVERFLOW) { console_reset_input(&console_state); }
        else if (result==0) { console_feed(&console_state,(uint8_t)raw); }
        else { return 80; }
        example_observe(2,raw);
    }
    if (liberums_call(SYS_KBD_RELEASE,console_state.keyboard.index,console_state.keyboard.generation,0,0,0)
        || liberums_call(SYS_VGA_RELEASE,console_state.vga.index,console_state.vga.generation,0,0,0)) { return 81; }
    return 0;
}
