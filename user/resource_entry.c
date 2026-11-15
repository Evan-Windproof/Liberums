/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "api.h"
__attribute__((section(".text.entry"))) _Noreturn void user_entry(void)
{
    const volatile uint32_t *data = (void *)(uintptr_t)USER_DATA;
    resource_program(data[0] == 0xaaaaaaaa ? 1 : 2, data[6]);
}
