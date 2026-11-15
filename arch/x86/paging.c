/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <arch/x86/paging.h>
#include <kernel/physical_pages.h>
#include <kernel/panic.h>
#include <kernel/user_abi.h>

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

/* Domain page structures are themselves supervisor-mapped physical RAM. */

static void clear_page(uint32_t address)
{
    volatile uint32_t *words = (void *)(uintptr_t)address;
    for (uint32_t i = 0; i < TABLE_ENTRIES; ++i) {
        words[i] = 0;
    }
}

void address_space_kernel(void)
{
    __asm__ volatile ("mov %0, %%cr3" : : "r"((uintptr_t)page_directory) : "memory");
}

void address_space_activate(const struct address_space *space)
{
    if (!space->live || space->count != USER_SPACE_PAGES) {
        panic("activating dead address space");
    }
    __asm__ volatile ("mov %0, %%cr3" : : "r"(space->pages[0]) : "memory");
}

void address_space_destroy(struct address_space *space)
{
    if (space == NULL || space->count > USER_SPACE_PAGES) {
        panic("invalid address space teardown");
    }
    uint32_t current_cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(current_cr3));
    if (space->count != 0 && current_cr3 == space->pages[0]) {
        panic("destroying active address space");
    }
    /* First remove every user alias; then zero backing pages before releasing.
     * Caller already switched CR3, flushing non-global translations on this CPU. */
    for (uint32_t i = 0; i < space->count; ++i) {
        clear_page(space->pages[i]);
    }
    for (uint32_t i = 0; i < space->count; ++i) {
        if (!physical_page_free(space->pages[i])) {
            panic("domain page release failed");
        }
    }
    space->live = false;
    space->count = 0;
}

bool address_space_create(struct address_space *space, const uint8_t *image,
                          uint32_t length, uint32_t probe)
{
    if (space == NULL || !enabled || space->live || space->count != 0 || image == NULL || length == 0 || length > PAGE_SIZE
        || (probe != USER_PROBE_A && probe != USER_PROBE_B)) {
        return false;
    }
    for (uint32_t i = 0; i < USER_SPACE_PAGES; ++i) {
        if (!physical_page_alloc(&space->pages[i])) {
            address_space_destroy(space);
            return false;
        }
        ++space->count;
        clear_page(space->pages[i]);
    }
    uint32_t *directory = (void *)(uintptr_t)space->pages[0];
    uint32_t *user_table = (void *)(uintptr_t)space->pages[1];
    uint32_t *probe_table = (void *)(uintptr_t)space->pages[2];
    for (uint32_t i = 0; i < TABLE_COUNT; ++i) {
        directory[i] = page_directory[i]; /* Existing supervisor mappings, U/S=0. */
    }
    directory[USER_CODE >> 22] = space->pages[1] | 7;
    directory[probe >> 22] = space->pages[2] | 7;
    user_table[0] = space->pages[3] | 5; /* User code is read-only, not NX capable. */
    user_table[1] = space->pages[4] | 7;
    user_table[2] = space->pages[5] | 7;
    probe_table[0] = space->pages[4] | 7; /* Alias only this domain's own data. */
    uint8_t *code = (void *)(uintptr_t)space->pages[3];
    for (uint32_t i = 0; i < length; ++i) {
        code[i] = image[i];
    }
    space->live = true;
    return true;
}
