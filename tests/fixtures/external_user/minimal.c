/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <abi/elf.h>
const struct liberums_abi_note requirement __attribute__((section(".note.liberums"), aligned(4), used)) = {
    9, 12, LIBERUMS_NOTE_TYPE, "LIBERUMS", 1, 0, LIBERUMS_FEATURE_PAGES
};
volatile uint32_t minimal_initialized = 0x11223344;
volatile uint32_t minimal_bss[8];
__attribute__((noinline)) void external_user_checkpoint(uint32_t stage)
{
    __asm__ volatile ("nop" : : "r"(stage) : "memory");
}
static void check(int condition, uint32_t status)
{
    if (!condition) {
        liberums_exit(status);
    }
}
__attribute__((section(".text.entry"))) _Noreturn void _start(void)
{
    external_user_checkpoint(1);
    uint32_t features, generation;
    uint32_t version = (uint32_t)liberums_call(SYS_ABI_QUERY, 0, 0, 0, 0, &features);
    check(liberums_abi_compatible(version, features, 1, 0, LIBERUMS_FEATURE_PAGES), 1);
    check((features & LIBERUMS_FEATURE_PAGES) != 0 && minimal_initialized == 0x11223344, 2);
    for (uint32_t i = 0; i < 8; ++i) {
        check(minimal_bss[i] == 0, 3);
    }
    int32_t index = liberums_call(SYS_PAGE_CLAIM, RESOURCE_ALL, 0, 0, 0, &generation);
    check(index > 0, 4);
    uint32_t va = RESOURCE_WINDOW + 0x2000;
    check(liberums_call(SYS_PAGE_MAP, index, generation, va, 3, 0) == 0, 5);
    volatile uint32_t *page = (void *)(uintptr_t)va;
    check(page[0] == 0, 6);
    page[0] = 0x12345678;
    external_user_checkpoint(2);
    liberums_call(SYS_LOG_CHAR, 'M', 0, 0, 0, 0);
    liberums_call(SYS_YIELD, 0, 0, 0, 0, 0);
    check(page[0] == 0x12345678, 7);
    check(liberums_call(SYS_PAGE_UNMAP, va, 0, 0, 0, 0) == 0, 8);
    check(liberums_call(SYS_PAGE_RELEASE, index, generation, 0, 0, 0) == 0, 9);
    external_user_checkpoint(3);
    liberums_exit(0);
}
