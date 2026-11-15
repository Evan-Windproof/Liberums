/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "api.h"
__attribute__((section(".text.entry"))) _Noreturn void user_entry(void)
{
    user_program(1);
}
