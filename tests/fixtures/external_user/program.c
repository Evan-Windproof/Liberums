/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <abi/elf.h>

const struct liberums_abi_note requirement __attribute__((section(".note.liberums"), aligned(4), used)) = {
    9, 12, LIBERUMS_NOTE_TYPE, "LIBERUMS", LIBERUMS_ABI_MAJOR, LIBERUMS_ABI_MINOR,
    LIBERUMS_FEATURE_PAGES | LIBERUMS_FEATURE_ELF32
};
const volatile uint8_t external_rodata[5000] = {[0] = 0x12, [4999] = 0x34};
volatile uint8_t external_data[5000] = {[0] = 0x56, [4999] = 0x78};
volatile uint8_t external_bss[9000];
extern uint32_t external_far_function(void);
static void check(int condition, uint32_t status)
{
    if (!condition) {
        liberums_exit(status);
    }
}
__attribute__((noinline)) void external_user_checkpoint(uint32_t stage)
{
    __asm__ volatile ("nop" : : "r"(stage) : "memory");
}
#if defined(TEST_FAULT) || defined(TEST_CODE_FAULT)
void external_fault(void);
#ifdef TEST_CODE_FAULT
__asm__(".text\n.global external_fault\nexternal_fault:\nmovb $0,0x08000000\nret\n");
#else
__asm__(".text\n.global external_fault\nexternal_fault:\nmovl 0x00100000,%eax\nret\n");
#endif
#endif

__attribute__((section(".text.entry"))) _Noreturn void _start(void)
{
    external_user_checkpoint(1);
    uint32_t features;
    uint32_t version = (uint32_t)liberums_call(SYS_ABI_QUERY, 0, 0, 0, 0, &features);
#ifdef TEST_RUNTIME_ABI
    /* Intentional mismatch: a client must reject a major it cannot understand. */
    if ((version >> 16) != LIBERUMS_ABI_MAJOR + 1) {
        liberums_exit(93);
    }
#endif
    check(liberums_abi_compatible(version, features, requirement.major, requirement.minor, requirement.required_features), 1);
    check(external_far_function() == 0x12345678, 2);
    check(external_rodata[0] == 0x12 && external_rodata[4999] == 0x34, 3);
    check(external_data[0] == 0x56 && external_data[4999] == 0x78, 4);
    for (uint32_t i = 0; i < sizeof(external_bss); ++i) {
        check(external_bss[i] == 0, 5);
        external_bss[i] = (uint8_t)i;
    }
    external_data[4999] = 0x99;
    uint32_t generation;
    int32_t index = liberums_call(SYS_PAGE_CLAIM, RESOURCE_ALL, 0, 0, 0, &generation);
    check(index > 0 && generation != 0, 6);
    uint32_t address = RESOURCE_WINDOW + 0x7000;
    check(liberums_call(SYS_PAGE_MAP, index, generation, address, RESOURCE_READ | RESOURCE_WRITE, 0) == 0, 7);
    volatile uint32_t *page = (void *)(uintptr_t)address;
    for (uint32_t i = 0; i < 1024; ++i) {
        check(page[i] == 0, 8);
        page[i] = 0xaabb0000 | i;
    }
    uint32_t extra_generation;
    uint32_t extra_index[3];
    uint32_t extra_gen[3];
    for (uint32_t i = 0; i < 3; ++i) {
        int32_t extra = liberums_call(SYS_PAGE_CLAIM, RESOURCE_ALL, 0, 0, 0, &extra_generation);
        check(extra > 0, 14);
        extra_index[i] = (uint32_t)extra;
        extra_gen[i] = extra_generation;
    }
    check(liberums_call(SYS_PAGE_CLAIM, RESOURCE_ALL, 0, 0, 0, 0) == RES_ENOSPC, 15);
    for (uint32_t i = 0; i < 3; ++i) {
        check(liberums_call(SYS_PAGE_RELEASE, extra_index[i], extra_gen[i], 0, 0, 0) == 0, 16);
    }
    external_user_checkpoint(2);
    liberums_call(SYS_LOG_CHAR, 'E', 0, 0, 0, 0);
    liberums_call(SYS_YIELD, 0, 0, 0, 0, 0);
    check(page[0] == 0xaabb0000 && page[1023] == 0xaabb03ff, 9);
    check(liberums_call(SYS_PAGE_UNMAP, address, 0, 0, 0, 0) == 0, 10);
    check(liberums_call(SYS_PAGE_RELEASE, index, generation, 0, 0, 0) == 0, 11);
#if defined(TEST_FAULT) || defined(TEST_CODE_FAULT)
    /* Leave a second live binding: fault cleanup must reclaim it. */
    index = liberums_call(SYS_PAGE_CLAIM, RESOURCE_ALL, 0, 0, 0, &generation);
    check(index > 0, 12);
    check(liberums_call(SYS_PAGE_MAP, index, generation, address, 3, 0) == 0, 13);
    external_fault();
#endif
    external_user_checkpoint(3);
    liberums_call(SYS_LOG_CHAR, 'e', 0, 0, 0, 0);
    liberums_exit(0);
}
