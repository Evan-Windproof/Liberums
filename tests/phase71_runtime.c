/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Explicit boot fixture: trusted launch grant, not a console service in Ring0. */
#include <kernel/phase71_test.h>
#include <kernel/vga_device.h>
#include <kernel/domain.h>
#include <kernel/boot_info.h>
#include <kernel/user_abi.h>
#include <kernel/physical_pages.h>
#include <kernel/panic.h>
#include <kernel/serial.h>
#include <kernel/resource_test.h>
#include <arch/x86/gdt.h>
static bool phase71_active;
static uint32_t phase71_mode, phase71_rounds, phase71_free_before;
static struct domain *volatile phase71_last;
extern const uint8_t vga_user_start[], vga_user_end[];
__attribute__((noinline)) void phase71_prepared(void) { __asm__ volatile ("" : : : "memory"); }
__attribute__((noinline)) void phase71_stopped(void) { __asm__ volatile ("" : : : "memory"); }
__attribute__((noinline)) void phase71_finished(void) { __asm__ volatile ("" : : : "memory"); }
__attribute__((noinline)) void phase71_reaped(void) { __asm__ volatile ("" : : : "memory"); }

void phase71_test_step(struct domain *domain)
{
    if (!phase71_active) { return; }
    phase71_last = domain;
    uint32_t *data = (void *)(uintptr_t)domain->space.pages[4];
    if (domain->id == 1 && data[2] == 1) {
        uint32_t *peer = (void *)(uintptr_t)domain_slot(2)->space.pages[4];
        peer[3] = data[3]; peer[4] = data[4];
    }
    if (domain->id == 1 && data[2] == 2 && phase71_mode == 1) {
        vga_device_revoke(domain);
    }
    phase71_stopped();
}

void phase71_test_run(const char *args)
{
    phase71_active = true;
    static const char *const options[] = {"vga-test=normal", "vga-test=revoke",
        "vga-test=upper", "vga-test=physical", "vga-test=readonly", "vga-test=kernel",
        "vga-test=io", "vga-test=table", "vga-test=release", "vga-test=unused", "vga-test=lower"};
    for (uint32_t i = 0; i < sizeof(options) / sizeof(options[0]); ++i) {
        if (boot_has_option(args, options[i])) { phase71_mode = i; }
    }
    gdt_init();
    for (uint32_t round = 0; round < 2; ++round) {
        phase71_free_before = physical_pages_free_count();
        for (uint32_t id = 1; id <= 2; ++id) {
            struct domain *domain = domain_slot(id);
            if (!address_space_create(&domain->space, vga_user_start,
                (uintptr_t)vga_user_end - (uintptr_t)vga_user_start,
                id == 1 ? USER_PROBE_A : USER_PROBE_B)) {
                panic("VGA fixture creation failed");
            }
            domain_initialize(domain, id, USER_CODE, USER_STACK_TOP);
            uint32_t *data = (void *)(uintptr_t)domain->space.pages[4];
            data[0] = id; data[1] = phase71_mode;
            data[5] = domain->space.pages[0];
        }
        if (!vga_device_grant(domain_slot(1), RESOURCE_READ | RESOURCE_WRITE)) {
            panic("VGA boot grant failed");
        }
        if (boot_has_option(args, "vga-test=oom")) {
            struct resource_handle h;
            uint32_t before = physical_pages_free_count();
            if (vga_device_acquire(domain_slot(1), VGA_DEVICE_TEXT, 3, &h) != 0) {
                panic("VGA OOM fixture acquire failed");
            }
            resource_test_pressure_begin(RESOURCE_TEST_CLAIM_FAILURE);
            if (physical_pages_free_count() != 0
                || vga_device_map(domain_slot(1), h, VGA_USER_ADDRESS, 3) != RES_ENOMEM
                || domain_slot(1)->space.device_table != 0) {
                panic("VGA OOM binding rollback failed");
            }
            resource_test_pressure_end();
            if (physical_pages_free_count() != before || vga_device_release(domain_slot(1), h) != 0) {
                panic("VGA OOM fixture cleanup failed");
            }
            serial_write("VGA: actual page-table OOM rollback passed\n");
        }
        phase71_prepared();
        domain_schedule();
        phase71_finished();
        for (uint32_t id = 1; id <= 2; ++id) {
            address_space_destroy(&domain_slot(id)->space);
        }
        if (physical_pages_free_count() != phase71_free_before) {
            panic("VGA page table leak");
        }
        ++phase71_rounds;
        phase71_reaped();
    }
    phase71_active = false;
    serial_write("Phase 7.1: VGA user experiment completed\n");
}
