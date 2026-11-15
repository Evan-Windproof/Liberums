/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_X86_USER_CONTEXT_H
#define LIBERUMS_X86_USER_CONTEXT_H
#include <arch/x86/idt.h>
/* The final two words exist only when interrupted CS has RPL 3. */
struct user_context {
    struct exception_frame cpu;
    uint32_t user_esp, user_ss;
};
_Static_assert(sizeof(struct user_context) == 60, "outer privilege frame size");
/* Returns to the kernel caller only via arch_user_leave, never via a user ret. */
void arch_user_enter(const struct user_context *context);
_Noreturn void arch_user_leave(void);
#endif
