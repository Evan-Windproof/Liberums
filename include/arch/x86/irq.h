/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_IRQ_H
#define LIBERUMS_IRQ_H
#include <arch/x86/user_context.h>
/* These words are saved before installing kernel selectors. No user pointers. */
struct segment_context { uint32_t gs, fs, es, ds; };
struct irq_frame {
    struct segment_context segments;
    struct exception_frame cpu;
    /* user ESP/SS follow only for a privilege transition. */
};
_Static_assert(sizeof(struct segment_context) == 16, "IRQ segment prefix");
void timer_irq_entry(void);
void keyboard_irq_entry(void);
void keyboard_dispatch(struct irq_frame *frame);
void timer_dispatch(struct irq_frame *frame);
#endif
