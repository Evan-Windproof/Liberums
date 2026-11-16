/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <arch/x86/paging.h>
#include <kernel/physical_pages.h>
#include <kernel/panic.h>
#include <kernel/user_abi.h>
#include <kernel/resource_abi.h>
#include <kernel/vga.h>
#include <abi/vga.h>
#include <kernel/external_launch.h>
#include <kernel/device_grants.h>

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
    struct external_config external = external_config_read(boot->commandline);
    if ((vga_available() && (external.devices & DEVICE_VGA_READ))
        || boot_has_option(boot->commandline, "vga-diag")
        || boot_has_option(boot->commandline, "vga-console")
        || boot_has_option(boot->commandline, "interactive-console")) {
        /* Supervisor-only MMIO alias; PCD/PWT match the later user binding. */
        page_tables[0][VGA_PHYSICAL / PAGE_SIZE] = VGA_PHYSICAL | 0x1bu;
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
    if (!space->live || (space->external ? space->count < 3 || space->count > ADDRESS_SPACE_PAGE_LIMIT
                                        : space->count != USER_SPACE_PAGES)) {
        panic("activating dead address space");
    }
    __asm__ volatile ("mov %0, %%cr3" : : "r"(space->pages[0]) : "memory");
}

void address_space_destroy(struct address_space *space)
{
    if (space == NULL || space->count > ADDRESS_SPACE_PAGE_LIMIT) {
        panic("invalid address space teardown");
    }
    if (space->dynamic_table != 0 || space->device_table != 0) {
        panic("destroying address space with resource bindings");
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
    space->external = false;
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

bool address_space_dynamic_address(uint32_t address)
{
    return address >= RESOURCE_WINDOW && address < RESOURCE_WINDOW_END
        && (address & (PAGE_SIZE - 1)) == 0;
}

static void flush_dynamic(struct address_space *space, uint32_t address, bool removed_table)
{
    uint32_t current;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(current));
    if (current == space->pages[0]) {
        if (removed_table) {
            /* Also discard paging-structure caches before releasing the table page. */
            __asm__ volatile ("mov %0, %%cr3" : : "r"(current) : "memory");
        } else {
            __asm__ volatile ("invlpg (%0)" : : "r"(address) : "memory");
        }
    }
    /* Inactive spaces cannot execute; CR3 reload flushes them before next entry. */
}

int32_t address_space_bind(struct address_space *space, uint32_t address, uint32_t physical, bool writable)
{
    if (!space->live || !address_space_dynamic_address(address)
        || (physical & (PAGE_SIZE - 1)) != 0 || physical_page_state(physical) != PAGE_ALLOCATED) {
        return RES_EINVAL;
    }
    uint32_t *directory = (void *)(uintptr_t)space->pages[0];
    if (space->dynamic_table == 0) {
        uint32_t table;
        if (!physical_page_alloc(&table)) {
            return RES_ENOMEM;
        }
        clear_page(table);
        space->dynamic_table = table;
        directory[RESOURCE_WINDOW >> 22] = table | 7;
    }
    uint32_t *table = (void *)(uintptr_t)space->dynamic_table;
    uint32_t index = (address - RESOURCE_WINDOW) / PAGE_SIZE;
    if (table[index] != 0) {
        return RES_EBUSY;
    }
    table[index] = physical | 5 | (writable ? 2 : 0);
    flush_dynamic(space, address, false);
    return 0;
}

bool address_space_unbind(struct address_space *space, uint32_t address, uint32_t physical)
{
    if (!space->live || !address_space_dynamic_address(address) || space->dynamic_table == 0) {
        return false;
    }
    uint32_t *table = (void *)(uintptr_t)space->dynamic_table;
    uint32_t index = (address - RESOURCE_WINDOW) / PAGE_SIZE;
    if ((table[index] & 0xfffff005) != (physical | 5)) {
        return false;
    }
    table[index] = 0;
    bool empty = true;
    for (uint32_t i = 0; i < TABLE_ENTRIES; ++i) {
        if (table[i] != 0) {
            empty = false;
            break;
        }
    }
    if (empty) {
        uint32_t *directory = (void *)(uintptr_t)space->pages[0];
        directory[RESOURCE_WINDOW >> 22] = 0;
    }
    flush_dynamic(space, address, empty);
    if (empty) {
        clear_page(space->dynamic_table);
        if (!physical_page_free(space->dynamic_table)) {
            panic("dynamic page table release failed");
        }
        space->dynamic_table = 0;
    }
    return true;
}

void paging_copy_kernel(uint32_t *directory)
{
    for (uint32_t i = 0; i < TABLE_COUNT; ++i) {
        directory[i] = page_directory[i];
    }
}

/* VGA is a fixed device binding in its own PDE. No caller-supplied PA, length,
 * cache flags, or page-table words cross this internal interface. */
int32_t address_space_vga_bind(struct address_space *space, bool writable)
{
    if (!space->live || !vga_available()) {
        return RES_EINVAL;
    }
    uint32_t *directory = (void *)(uintptr_t)space->pages[0];
    if (space->device_table != 0 || directory[VGA_USER_ADDRESS >> 22] != 0) {
        return RES_EBUSY;
    }
    uint32_t table;
    if (!physical_page_alloc(&table)) {
        return RES_ENOMEM;
    }
    clear_page(table);
    uint32_t *entries = (void *)(uintptr_t)table;
    /* User, present, optionally writable, PWT+PCD: UC after boot has verified PAT[3].
     * The kernel's supervisor alias has matching cache attributes. */
    entries[0] = VGA_PHYSICAL | 0x1du | (writable ? 2u : 0u);
    space->device_table = table;
    directory[VGA_USER_ADDRESS >> 22] = table | 7u;
    flush_dynamic(space, VGA_USER_ADDRESS, true);
    return 0;
}

bool address_space_vga_unbind(struct address_space *space)
{
    if (!space->live || space->device_table == 0) {
        return false;
    }
    uint32_t *directory = (void *)(uintptr_t)space->pages[0];
    uint32_t *table = (void *)(uintptr_t)space->device_table;
    if ((table[0] & 0xfffff09du) != (VGA_PHYSICAL | 0x1du)
        || (directory[VGA_USER_ADDRESS >> 22] & ~0x20u) != (space->device_table | 7u)) {
        return false;
    }
    table[0] = 0;
    directory[VGA_USER_ADDRESS >> 22] = 0;
    flush_dynamic(space, VGA_USER_ADDRESS, true);
    /* Only this RAM page table is zeroed/freed, never the VGA device page. */
    clear_page(space->device_table);
    if (!physical_page_free(space->device_table)) {
        panic("VGA page table release failed");
    }
    space->device_table = 0;
    return true;
}
