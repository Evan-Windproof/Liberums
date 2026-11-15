/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/selftest.h>
#include <kernel/panic.h>
#include <kernel/physical_pages.h>
#include <kernel/serial.h>
#include <arch/x86/paging.h>
#include <arch/x86/test_faults.h>

extern char __kernel_start[], __kernel_end[];
static bool seen[PHYSICAL_LIMIT / PAGE_SIZE];

static bool option(const char *commandline, const char *wanted)
{
    while (*commandline != '\0') {
        while (*commandline == ' ') {
            ++commandline;
        }
        const char *token = commandline;
        const char *expected = wanted;
        while (*expected != '\0' && *token == *expected) {
            ++token;
            ++expected;
        }
        if (*expected == '\0' && (*token == ' ' || *token == '\0')) {
            return true;
        }
        while (*commandline != '\0' && *commandline != ' ') {
            ++commandline;
        }
    }
    return false;
}

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
    if (option(args, "selftest=pages")) {
        test_physical_pages(boot);
        return;
    }
    if (option(args, "selftest=ud")) {
        test_ud();
    } else if (option(args, "selftest=divide")) {
        test_divide();
    } else if (option(args, "selftest=gp")) {
        test_gp();
    } else if (option(args, "selftest=pf-read")) {
        test_pf_read();
    } else if (option(args, "selftest=pf-null")) {
        test_pf_null();
    } else if (option(args, "selftest=ro-write")) {
        test_ro_write();
    } else if (option(args, "selftest=tlb")) {
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
