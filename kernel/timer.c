/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <arch/x86/irq.h>
#include <arch/x86/pic.h>
#include <arch/x86/pit.h>
#include <kernel/timer.h>
#include <kernel/tick.h>
#include <kernel/domain.h>
#include <abi/syscall.h>
static bool enabled, exhausted;
static uint32_t ticks, irq_count, eoi_count, kernel_irqs;
void timer_start(void)
{
    /* Caller has IF=0; installing IRQ0 never enables interrupts in kernel C. */
    pit_init();
    pic_init_timer();
    enabled = true;
}
void timer_stop(void) { pic_mask_all(); }
bool timer_enabled(void) { return enabled; }
bool timer_exhausted(void) { return exhausted; }
uint32_t timer_now(void) { return ticks; }
uint32_t abi_features(void)
{
    return enabled ? LIBERUMS_PREEMPT_FEATURES : LIBERUMS_FEATURES;
}
void timer_dispatch(struct irq_frame *frame)
{
    ++irq_count;
    if (!tick_advance(&ticks) || ticks == UINT32_MAX) {
        exhausted = true;
        timer_stop(); /* No wrap; scheduler terminates remaining domains. */
    }
    pic_timer_eoi();
    ++eoi_count; /* Exactly once, BEFORE leaving the interrupt stack. */
    if ((frame->cpu.cs & 3) == 3) {
        domain_preempt(&frame->cpu, &frame->segments);
    }
    ++kernel_irqs; /* No switch/free on a kernel-origin IRQ. */
}
