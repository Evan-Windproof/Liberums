/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_ARCH_X86_PAGING_H
#define LIBERUMS_ARCH_X86_PAGING_H
#include <kernel/boot_info.h>

bool paging_init(const struct boot_context *boot);
/* Early kernel only; cannot remove permanent kernel/boot mappings. */
bool paging_unmap_allocated(uint32_t address);
/* Only the kernel constructs this fixed layout; no arbitrary mapping requests. */
#define USER_SPACE_PAGES 6
#define ADDRESS_SPACE_PAGE_LIMIT 70
struct elf_plan;
struct address_space {
    uint32_t pages[ADDRESS_SPACE_PAGE_LIMIT]; /* directory, two tables, code, data, stack */
    uint32_t count;
    uint32_t dynamic_table; /* Lazy page table for the resource window, not a base page. */
    uint32_t device_table; /* Private VGA binding table; never device memory. */
    bool live;
    bool external;
    uint32_t rollback_pages; /* Diagnostic count before a failed external build was unwound. */
};
bool address_space_create(struct address_space *space, const uint8_t *image,
                          uint32_t length, uint32_t probe);
void address_space_activate(const struct address_space *space);
void address_space_kernel(void);
void address_space_destroy(struct address_space *space);
bool address_space_dynamic_address(uint32_t address);
int32_t address_space_bind(struct address_space *space, uint32_t address, uint32_t physical, bool writable);
bool address_space_unbind(struct address_space *space, uint32_t address, uint32_t physical);
int32_t address_space_vga_bind(struct address_space *space, bool writable);
bool address_space_vga_unbind(struct address_space *space);
void paging_copy_kernel(uint32_t *directory);
/* Requires a successful elf_validate plan and immutable module bytes. */
int32_t address_space_create_external(struct address_space *space, const uint8_t *image,
                                      const struct elf_plan *plan);
#endif
