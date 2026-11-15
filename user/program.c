/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "api.h"
__attribute__((noinline)) void user_checkpoint(void)
{
    __asm__ volatile ("nop" : : : "memory");
}
_Noreturn void user_program(uint32_t id)
{
    volatile uint32_t *data = (void *)(uintptr_t)USER_DATA;
    uint32_t marker = id == 1 ? 0xaaaaaaaa : 0xbbbbbbbb;
    user_checkpoint(); /* GDB observes CPL 3 before the first syscall. */
    if (data[0] != marker || data[8] != 0
        || *(volatile uint32_t *)(uintptr_t)data[3] != marker) {
        user_exit(10);
    }
    data[8] = marker;
    if (data[1] != USER_NORMAL) {
        uint32_t mode = data[1];
        uint32_t target = data[2];
        if (mode == USER_TABLE_WRITE) {
            target = data[4];
        } else if (mode == USER_PHYSICAL_READ) {
            target = data[5];
        }
        user_fault(mode, target);
        user_exit(11); /* Any denial experiment returning is a test failure. */
    }
    if (user_call(0xffffffff, 0xffffffff) != -38
        || user_call(SYS_LOG_CHAR, 256) != -22) {
        user_exit(12);
    }
    user_call(SYS_LOG_CHAR, id == 1 ? 'A' : 'B');
    if (user_register_test() != 0 || data[8] != marker) {
        user_exit(13);
    }
    user_call(SYS_LOG_CHAR, id == 1 ? 'a' : 'b');
    user_exit(0);
}
