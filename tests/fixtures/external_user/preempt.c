/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Independently linked external program; uses public SDK only. */
#include <abi/elf.h>
const struct liberums_abi_note requirement __attribute__((section(".note.liberums"), aligned(4), used)) = {
    9, 12, LIBERUMS_NOTE_TYPE, "LIBERUMS", 1, 2,
    LIBERUMS_FEATURE_PAGES | LIBERUMS_FEATURE_PREEMPTION | LIBERUMS_FEATURE_REVOCATION
};
volatile uint32_t external_counter, external_features, external_version;
__attribute__((noinline)) void external_preempt_checkpoint(uint32_t stage)
{
    __asm__ volatile ("nop" : : "r"(stage) : "memory");
}
static void check(int condition, uint32_t status)
{
    if (!condition) { liberums_exit(status); }
}
__attribute__((section(".text.entry"))) _Noreturn void _start(void)
{
    uint32_t features, generation;
    external_version = (uint32_t)liberums_call(SYS_ABI_QUERY, 0, 0, 0, 0, &features);
    external_features = features;
    check(liberums_abi_compatible(external_version, features, 1, 2, requirement.required_features), 1);
    check(!(features & LIBERUMS_FEATURE_COOPERATIVE), 2);
    int32_t index = liberums_call(SYS_PAGE_CLAIM, RESOURCE_ALL, 0, 0, 0, &generation);
    check(index > 0, 3);
    check(liberums_call(SYS_PAGE_MAP, index, generation, RESOURCE_WINDOW, 3, 0) == 0, 4);
    volatile uint32_t *page = (void *)(uintptr_t)RESOURCE_WINDOW;
    page[0] = 0x600dbeef;
    external_preempt_checkpoint(1);
    /* Finite CPU work, no yield. PIT must run B while this loop is active. */
    for (external_counter = 0; external_counter < 20000000; ++external_counter) {
        __asm__ volatile ("" : : : "memory");
    }
    check(page[0] == 0x600dbeef, 5);
    external_preempt_checkpoint(2);
    check(liberums_call(SYS_PAGE_RELEASE, index, generation, 0, 0, 0) == 0, 6);
    liberums_exit(0);
}
