/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/selftest.h>
#include <kernel/panic.h>
#include <kernel/physical_pages.h>
#include <kernel/serial.h>
#include <kernel/user_abi.h>
#include <arch/x86/paging.h>
#include <arch/x86/test_faults.h>

extern char __kernel_start[], __kernel_end[];
static bool seen[PHYSICAL_LIMIT / PAGE_SIZE];

static void check(bool condition, const char *reason)
{
    if (!condition) {
        panic(reason);
    }
}

static void test_physical_pages(const struct boot_context *boot)
{
    for (uint32_t address = 0; address < 0x100000; address += PAGE_SIZE) {
        check(physical_page_state(address) == PAGE_RESERVED, "firmware reservation test");
    }
    for (uint32_t address = (uintptr_t)__kernel_start; address < (uintptr_t)__kernel_end;
         address += PAGE_SIZE) {
        check(physical_page_state(address) == PAGE_KERNEL, "kernel reservation test");
        check(!physical_page_free(address), "permanent kernel page released");
    }
    for (size_t i = 0; i < boot->reserved_count; ++i) {
        for (uint32_t address = boot->reserved[i].begin & ~(PAGE_SIZE - 1);
             address < boot->reserved[i].end; address += PAGE_SIZE) {
            check(physical_page_state(address) != PAGE_FREE, "boot reservation test");
        }
    }
    uint32_t initial = physical_pages_free_count();
    uint32_t address, count = 0;
    while (physical_page_alloc(&address)) {
        check(!seen[address / PAGE_SIZE], "duplicate physical page allocation");
        check(physical_page_state(address) == PAGE_ALLOCATED, "allocation state test");
        check(memory_map_contains_ram(&boot->map, address, (uint64_t)address + PAGE_SIZE),
              "allocated non-RAM page");
        seen[address / PAGE_SIZE] = true;
        ++count;
    }
    check(count == initial && count != 0 && physical_pages_free_count() == 0,
          "physical page exhaustion test");
    for (uint32_t page = 0; page < PHYSICAL_LIMIT / PAGE_SIZE; ++page) {
        if (seen[page]) {
            check(physical_page_free(page * PAGE_SIZE), "physical page release test");
            check(!physical_page_free(page * PAGE_SIZE), "double free accepted");
        }
    }
    check(physical_pages_free_count() == initial, "free page count mismatch");
    uint32_t first, second;
    check(physical_page_alloc(&first) && physical_page_alloc(&second) && first != second,
          "reallocation test");
    check(physical_page_free(first) && physical_page_free(second), "reallocation cleanup");
    serial_write("SELFTEST: physical reservations, exhaustion, uniqueness and reuse passed\n");
}

void selftest_run(const struct boot_context *boot)
{
    const char *args = boot->commandline;
    if (boot_has_option(args, "selftest=pages")) {
        test_physical_pages(boot);
        return;
    }
    if (boot_has_option(args, "selftest=ud")) {
        test_ud();
    } else if (boot_has_option(args, "selftest=divide")) {
        test_divide();
    } else if (boot_has_option(args, "selftest=gp")) {
        test_gp();
    } else if (boot_has_option(args, "selftest=pf-read")) {
        test_pf_read();
    } else if (boot_has_option(args, "selftest=pf-null")) {
        test_pf_null();
    } else if (boot_has_option(args, "selftest=ro-write")) {
        test_ro_write();
    } else if (boot_has_option(args, "selftest=tlb")) {
        uint32_t address;
        check(physical_page_alloc(&address), "TLB test allocation failed");
        *(volatile uint32_t *)(uintptr_t)address = 0x12345678; /* Populate the TLB. */
        check(*(volatile uint32_t *)(uintptr_t)address == 0x12345678, "TLB warm-up failed");
        check(paging_unmap_allocated(address), "TLB test unmap failed");
        serial_write("test-address=");
        serial_hex(address);
        serial_write("\n");
        test_unmapped(address);
    } else {
        return;
    }
    panic("fault selftest unexpectedly returned");
}

void selftest_address_space_failure(void)
{
    uint32_t initial = physical_pages_free_count();
    check(initial > 6, "insufficient RAM for allocation failure test");
    uint32_t held = 0;
    /* Store a temporary linked list in held pages, avoiding a kernel heap. */
    for (uint32_t i = 0; i < initial - 3; ++i) {
        uint32_t page;
        check(physical_page_alloc(&page), "failure test could not reserve RAM");
        *(uint32_t *)(uintptr_t)page = held;
        held = page;
    }
    struct address_space trial = {0};
    const uint8_t image[] = {0xc3};
    check(!address_space_create(&trial, image, sizeof(image), USER_PROBE_A),
          "address space succeeded without enough pages");
    check(!trial.live && trial.count == 0 && physical_pages_free_count() == 3,
          "partial address space leaked pages");
    for (uint32_t i = 0; i < 3; ++i) {
        check(physical_page_state(trial.pages[i]) == PAGE_FREE, "partial page not released");
        const uint32_t *words = (const void *)(uintptr_t)trial.pages[i];
        for (uint32_t word = 0; word < PAGE_SIZE / sizeof(uint32_t); ++word) {
            check(words[word] == 0, "partial page not cleared");
        }
    }
    while (held != 0) {
        uint32_t next = *(uint32_t *)(uintptr_t)held;
        *(uint32_t *)(uintptr_t)held = 0;
        check(physical_page_free(held), "failure test cleanup failed");
        held = next;
    }
    check(physical_pages_free_count() == initial, "failure test leaked held pages");
    serial_write("SELFTEST: partial address-space allocation rolled back\n");
}
