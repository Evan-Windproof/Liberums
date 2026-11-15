/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <arch/x86/paging.h>
#include <kernel/physical_pages.h>

#define TABLE_ENTRIES 1024
#define TABLE_COUNT (PHYSICAL_LIMIT / (PAGE_SIZE * TABLE_ENTRIES))
#define PTE_PRESENT UINT32_C(1)
#define PTE_WRITE UINT32_C(2)

static uint32_t page_directory[TABLE_ENTRIES] __attribute__((aligned(4096)));
static uint32_t page_tables[TABLE_COUNT][TABLE_ENTRIES] __attribute__((aligned(4096)));
static bool enabled;
extern char __kernel_start[], __kernel_readonly_end[];

static bool boot_page_needed(const struct boot_context *boot, uint32_t address)
{
    for (size_t i = 0; i < boot->reserved_count; ++i) {
        if (address < boot->reserved[i].end && boot->reserved[i].begin < address + PAGE_SIZE) {
            return true;
        }
    }
    return false;
}

bool paging_init(const struct boot_context *boot)
{
    if (enabled || boot == NULL) {
        return false;
    }
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    if (cr0 & UINT32_C(0x80000000)) {
        return false;
    }
    for (uint32_t table = 0; table < TABLE_COUNT; ++table) {
        /* All entries are supervisor-only: user/supervisor bit remains zero. */
        page_directory[table] = (uintptr_t)page_tables[table] | PTE_PRESENT | PTE_WRITE;
        for (uint32_t index = 0; index < TABLE_ENTRIES; ++index) {
            uint32_t address = (table * TABLE_ENTRIES + index) * PAGE_SIZE;
            bool needed = physical_page_state(address) == PAGE_KERNEL
                || memory_map_contains_ram(&boot->map, address, (uint64_t)address + PAGE_SIZE)
                || boot_page_needed(boot, address);
            if (address == 0 || !needed) {
                continue; /* Null page, non-RAM holes and out-of-range memory stay absent. */
            }
            uint32_t permissions = PTE_PRESENT;
            if (address < (uintptr_t)__kernel_start || address >= (uintptr_t)__kernel_readonly_end) {
                permissions |= PTE_WRITE;
            }
            page_tables[table][index] = address | permissions;
        }
    }
    __asm__ volatile ("mov %0, %%cr3" : : "r"((uintptr_t)page_directory) : "memory");
    cr0 |= UINT32_C(0x80010000); /* PG plus WP: even ring 0 respects read-only PTEs. */
    __asm__ volatile ("mov %0, %%cr0" : : "r"(cr0) : "memory");
    enabled = true;
    return true;
}

bool paging_unmap_allocated(uint32_t address)
{
    if (!enabled || (address & (PAGE_SIZE - 1)) != 0
        || physical_page_state(address) != PAGE_ALLOCATED) {
        return false;
    }
    uint32_t page = address / PAGE_SIZE;
    uint32_t *entry = &page_tables[page / TABLE_ENTRIES][page % TABLE_ENTRIES];
    if (!(*entry & PTE_PRESENT)) {
        return false;
    }
    *entry = 0;
    /* Single CPU, no global mappings: remove the cached translation immediately. */
    __asm__ volatile ("invlpg (%0)" : : "r"(address) : "memory");
    return true;
}
