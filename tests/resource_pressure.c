/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/resource_test.h>
#include <kernel/boot_info.h>
#include <kernel/physical_pages.h>
#include <kernel/panic.h>
static uint32_t held_pages;
enum resource_test_mode resource_test_select(const char *args)
{
    static const char *const names[] = {
        "resource-test=none", "resource-test=basic", "resource-test=transfer",
        "resource-test=unmap-fault", "resource-test=release-fault", "resource-test=readonly",
        "resource-test=transfer-fault", "resource-test=exit", "resource-test=generation",
        "resource-test=map-failure", "resource-test=claim-failure"
    };
    for (uint32_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        if (boot_has_option(args, names[i])) {
            return (enum resource_test_mode)i;
        }
    }
    return RESOURCE_TEST_NONE;
}
void resource_test_pressure_begin(enum resource_test_mode mode)
{
    if (mode != RESOURCE_TEST_MAP_FAILURE && mode != RESOURCE_TEST_CLAIM_FAILURE) {
        return;
    }
    uint32_t available = physical_pages_free_count();
    uint32_t remaining = mode == RESOURCE_TEST_MAP_FAILURE ? 1 : 0;
    for (uint32_t i = remaining; i < available; ++i) {
        uint32_t address;
        if (!physical_page_alloc(&address)) {
            panic("resource pressure setup failed");
        }
        *(uint32_t *)(uintptr_t)address = held_pages;
        held_pages = address;
    }
}
void resource_test_pressure_end(void)
{
    while (held_pages != 0) {
        uint32_t next = *(uint32_t *)(uintptr_t)held_pages;
        *(uint32_t *)(uintptr_t)held_pages = 0;
        if (!physical_page_free(held_pages)) {
            panic("resource pressure cleanup failed");
        }
        held_pages = next;
    }
}
