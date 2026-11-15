/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/external.h>
#include <kernel/physical_pages.h>
#include <kernel/panic.h>
static uint32_t held;
void elf_test_pressure_begin(bool enabled)
{
    if (!enabled) {
        return;
    }
    uint32_t free_pages = physical_pages_free_count();
    for (uint32_t i = 5; i < free_pages; ++i) {
        uint32_t page;
        if (!physical_page_alloc(&page)) {
            panic("ELF pressure setup");
        }
        *(uint32_t *)(uintptr_t)page = held;
        held = page;
    }
}
void elf_test_pressure_end(void)
{
    while (held != 0) {
        uint32_t next = *(uint32_t *)(uintptr_t)held;
        *(uint32_t *)(uintptr_t)held = 0;
        if (!physical_page_free(held)) {
            panic("ELF pressure teardown");
        }
        held = next;
    }
}
