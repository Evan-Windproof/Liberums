/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Explicit interactive launcher and lifetime fixture. No keymap/editor policy. */
#include <kernel/phase72_test.h>
#include <kernel/domain.h>
#include <kernel/keyboard.h>
#include <kernel/vga_device.h>
#include <kernel/user_abi.h>
#include <kernel/boot_info.h>
#include <kernel/physical_pages.h>
#include <kernel/serial.h>
#include <kernel/panic.h>
#include <kernel/resource.h>
#include <arch/x86/gdt.h>
static bool phase72_active;
static uint32_t phase72_rounds, phase72_free_before;
extern const uint8_t interactive_user_start[], interactive_user_end[];
__attribute__((noinline)) void phase72_finished(void) { __asm__ volatile ("" : : : "memory"); }
__attribute__((noinline)) void phase72_reaped(void) { __asm__ volatile ("" : : : "memory"); }
void phase72_step(struct domain *domain)
{
    if (!phase72_active || domain->id != 1) { return; }
    uint32_t *data = (void *)(uintptr_t)domain->space.pages[4];
    struct domain *peer = domain_slot(2);
    if (data[3] != 0) {
        uint32_t *peer_data = (void *)(uintptr_t)peer->space.pages[4];
        peer_data[3] = data[3]; peer_data[4] = data[4]; peer_data[5] = data[5];
    }
    if (data[9] == 6 && domain->state == DOMAIN_READY) { keyboard_revoke(domain); }
    if (domain->state == DOMAIN_EXITED || domain->state == DOMAIN_FAULTED) {
        /* End only this fixture's non-cooperative peer after the owner ends. */
        peer->state = DOMAIN_EXITED;
        keyboard_revoke(peer); vga_device_revoke(peer); resource_cleanup(peer);
    }
}
void phase72_run(const char *args)
{
    phase72_active = true;
    gdt_init();
    uint32_t rounds = boot_has_option(args, "console-test") ? 2 : 1;
    for (uint32_t round = 0; round < rounds; ++round) {
        phase72_free_before = physical_pages_free_count();
        for (uint32_t id = 1; id <= 2; ++id) {
            struct domain *domain = domain_slot(id);
            if (!address_space_create(&domain->space, interactive_user_start,
                (uintptr_t)interactive_user_end - (uintptr_t)interactive_user_start,
                id == 1 ? USER_PROBE_A : USER_PROBE_B)) { panic("interactive domain creation failed"); }
            domain_initialize(domain, id, USER_CODE, USER_STACK_TOP);
            uint32_t *data = (void *)(uintptr_t)domain->space.pages[4];
            data[0] = id;
            data[1] = boot_has_option(args, "console-overflow") ? 2 : 0;
        }
        if (!keyboard_start() || !keyboard_grant(domain_slot(1))
            || !vga_device_grant(domain_slot(1), RESOURCE_READ | RESOURCE_WRITE)) {
            panic("interactive device grant failed");
        }
        serial_write("Interactive console: PS/2 IRQ1 ready\n");
        domain_schedule();
        phase72_finished();
        for (uint32_t id = 1; id <= 2; ++id) { address_space_destroy(&domain_slot(id)->space); }
        if (physical_pages_free_count() != phase72_free_before) { panic("interactive page leak"); }
        ++phase72_rounds;
        phase72_reaped();
    }
    phase72_active = false;
    serial_write("Phase 7.2: interactive lifetimes completed\n");
}
