/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_ARCH_X86_IDT_H
#define LIBERUMS_ARCH_X86_IDT_H

#include <stdint.h>

/* pushal order followed by the normalized vector/error and CPU frame.
 * For ring 3, user_context adds the outer ESP/SS after this common prefix. */
struct exception_frame {
    uint32_t edi, esi, ebp, saved_esp;
    uint32_t ebx, edx, ecx, eax;
    uint32_t vector, error;
    uint32_t eip, cs, eflags;
};

/* Address table exported by isr.S for the IDT builder. */
extern void (*const isr_entries[32])(void);
void idt_init(void);
_Noreturn void exception_dispatch(struct exception_frame *frame);
#endif
