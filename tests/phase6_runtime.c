/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Explicit diagnostic boot paths only. No production pressure policy is hidden here. */
#include <kernel/phase6_test.h>
#include <kernel/domain.h>
#include <kernel/user_abi.h>
#include <kernel/boot_info.h>
#include <kernel/revoke.h>
#include <kernel/timer.h>
#include <kernel/panic.h>
#include <kernel/physical_pages.h>
#include <kernel/serial.h>
#include <arch/x86/gdt.h>
static uint32_t test_mode, free_before_phase6, phase6_rounds;
static bool requested;
static uint32_t test_request;
extern const uint8_t phase6_user_start[], phase6_user_end[];
__attribute__((noinline)) void phase6_prepared(void)
{
    __asm__ volatile ("" : : : "memory");
}
__attribute__((noinline)) void phase6_requested(void)
{
    __asm__ volatile ("" : : : "memory");
}
__attribute__((noinline)) void phase6_reaped(void)
{
    __asm__ volatile ("" : : : "memory");
}
bool phase6_test_selected(const char *args)
{
    return boot_has_option(args, "phase6=spin") || boot_has_option(args, "phase6=ack")
        || boot_has_option(args, "phase6=force") || boot_has_option(args, "phase6=release")
        || boot_has_option(args, "phase6=exit-pending");
}
void phase6_test_step(struct domain *domain)
{
    if (test_mode == 0) {
        return;
    }
    struct domain *owner = domain_slot(1);
    struct domain *peer = domain_slot(2);
    uint32_t *a = (void *)(uintptr_t)owner->space.pages[4];
    uint32_t *b = (void *)(uintptr_t)peer->space.pages[4];
    if (test_mode != 1 && !requested && a[2] == 1) {
        struct resource_handle handle = {a[3], a[4]};
        /* Diagnostic metadata only supplies scalars. The production request
         * validates owner, epoch, generation and state independently. */
        if (revoke_request(owner, handle, test_mode == 3 ? 6 : 1000, REVOKE_REASON_TEST) != 0
            || revoke_poll(owner, &test_request) != 0) {
            panic("phase6 diagnostic request failed");
        }
        requested = true;
        b[2] = 1;
        b[3] = a[3];
        b[4] = a[4];
        b[5] = a[5];
        b[8] = test_request;
        phase6_requested();
    }
    /* Explicit diagnostic CPU budget: guarantees the infinite spin experiment
     * ends. Normal preempt mode has no implicit domain execution limit. */
    if (domain->state == DOMAIN_READY && domain->preemptions >= 20) {
        domain->exit_status = (uint32_t)RES_EBUSY;
        domain->state = DOMAIN_EXITED;
    }
}
void phase6_test_run(const char *args)
{
    test_mode = boot_has_option(args, "phase6=spin") ? 1
        : boot_has_option(args, "phase6=force") ? 3
        : boot_has_option(args, "phase6=release") ? 4
        : boot_has_option(args, "phase6=exit-pending") ? 5 : 2;
    gdt_init();
    /* Exercise kernel-origin IRQ without touching a resource critical section.
     * HLT waits for actual IRQ0; interrupt shadow guarantees HLT follows STI. */
    __asm__ volatile ("sti; hlt; cli; .global phase6_kernel_irq_return; phase6_kernel_irq_return:"
                      : : : "memory");
    for (uint32_t round = 0; round < 2; ++round) {
        free_before_phase6 = physical_pages_free_count();
        requested = false;
        for (uint32_t id = 1; id <= 2; ++id) {
            struct domain *domain = domain_slot(id);
            if (!address_space_create(&domain->space, phase6_user_start,
                (uintptr_t)phase6_user_end - (uintptr_t)phase6_user_start,
                id == 1 ? USER_PROBE_A : USER_PROBE_B)) {
                panic("phase6 diagnostic creation failed");
            }
            domain_initialize(domain, id, USER_CODE, USER_STACK_TOP);
            uint32_t *data = (void *)(uintptr_t)domain->space.pages[4];
            data[0] = id;
            data[1] = test_mode;
        }
        phase6_prepared();
        domain_schedule();
        for (uint32_t id = 1; id <= 2; ++id) {
            address_space_destroy(&domain_slot(id)->space);
        }
        if (physical_pages_free_count() != free_before_phase6) {
            panic("phase6 diagnostic page leak");
        }
        ++phase6_rounds;
        phase6_reaped();
    }
    serial_write("Phase 6: experiments completed\n");
}
