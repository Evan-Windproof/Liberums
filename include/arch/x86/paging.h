/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_ARCH_X86_PAGING_H
#define LIBERUMS_ARCH_X86_PAGING_H
#include <kernel/boot_info.h>

bool paging_init(const struct boot_context *boot);
/* Early kernel only; cannot remove permanent kernel/boot mappings. */
bool paging_unmap_allocated(uint32_t address);
/* Only the kernel constructs this fixed layout; no arbitrary mapping requests. */
#define USER_SPACE_PAGES 6
struct address_space {
    uint32_t pages[USER_SPACE_PAGES]; /* directory, two tables, code, data, stack */
    uint32_t count;
    uint32_t dynamic_table; /* Lazy page table for the resource window, not a base page. */
    bool live;
};
bool address_space_create(struct address_space *space, const uint8_t *image,
                          uint32_t length, uint32_t probe);
void address_space_activate(const struct address_space *space);
void address_space_kernel(void);
void address_space_destroy(struct address_space *space);
bool address_space_dynamic_address(uint32_t address);
int32_t address_space_bind(struct address_space *space, uint32_t address, uint32_t physical, bool writable);
bool address_space_unbind(struct address_space *space, uint32_t address, uint32_t physical);
#endif
