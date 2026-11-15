/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_USER_API_H
#define LIBERUMS_USER_API_H
#include <stdint.h>
#include <kernel/user_abi.h>
static inline int32_t user_call(uint32_t number, uint32_t argument)
{
    __asm__ volatile ("int $0x80" : "+a"(number) : "b"(argument) : "memory", "cc");
    return (int32_t)number;
}
static inline _Noreturn void user_exit(uint32_t status)
{
    user_call(SYS_EXIT, status);
    for (;;) { __asm__ volatile ("ud2"); }
}
void user_checkpoint(void);
void user_fault(uint32_t mode, uint32_t target);
uint32_t user_register_test(void);
_Noreturn void user_program(uint32_t id);
#endif
