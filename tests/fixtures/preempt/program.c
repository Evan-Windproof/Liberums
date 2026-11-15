/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <abi/syscall.h>
#define DATA ((volatile uint32_t *)(uintptr_t)0x08001000u)
#define PAGE ((volatile uint32_t *)(uintptr_t)RESOURCE_WINDOW)
extern _Noreturn void phase6_spin(void);
extern _Noreturn void phase6_stale_write(void);
__attribute__((noinline)) void phase6_user_checkpoint(uint32_t stage)
{
    __asm__ volatile ("nop" : : "r"(stage) : "memory");
}
static void check(int condition, uint32_t status)
{
    if (!condition) {
        liberums_exit(status);
    }
}
static int32_t call(uint32_t number, uint32_t bx, uint32_t cx, uint32_t dx, uint32_t si)
{
    return liberums_call(number, bx, cx, dx, si, 0);
}
static _Noreturn void peer(void)
{
    if (DATA[1] == 1) {
        phase6_user_checkpoint(2);
        liberums_exit(0); /* Must run despite A's infinite loop. */
    }
    while (!DATA[2]) {
        call(SYS_YIELD, 0, 0, 0, 0);
    }
    check(call(SYS_REVOKE_ACK, DATA[3], DATA[4], DATA[8], 0) == RES_ESTALE, 40);
    uint32_t generation;
    int32_t index;
    do {
        index = liberums_call(SYS_PAGE_SPECIFIC, DATA[5], RESOURCE_ALL, 0, 0, &generation);
        if (index == RES_EBUSY) {
            call(SYS_YIELD, 0, 0, 0, 0);
        }
    } while (index == RES_EBUSY);
    check(index > 0 && generation > DATA[4], 41);
    check(call(SYS_PAGE_MAP, index, generation, RESOURCE_WINDOW, 3) == 0, 42);
    for (uint32_t i = 0; i < 1024; ++i) {
        check(PAGE[i] == 0, 43);
        PAGE[i] = 0xb0000000u | i;
    }
    DATA[10] = 0xb600d;
    phase6_user_checkpoint(3);
    call(SYS_YIELD, 0, 0, 0, 0);
    for (uint32_t i = 0; i < 1024; ++i) {
        check(PAGE[i] == (0xb0000000u | i), 44);
    }
    check(call(SYS_REVOKE_ACK, DATA[3], DATA[4], DATA[8], 0) == RES_ESTALE, 45);
    phase6_user_checkpoint(4);
    liberums_exit(0); /* Teardown must also reclaim this live resource. */
}
_Noreturn void phase6_user_main(void)
{
    uint32_t features;
    uint32_t version = (uint32_t)liberums_call(SYS_ABI_QUERY, 0, 0, 0, 0, &features);
    check(liberums_abi_compatible(version, features, 1, 2, 96)
          && !(features & LIBERUMS_FEATURE_COOPERATIVE), 1);
    if (DATA[0] == 2) {
        peer();
    }
    if (DATA[1] == 1) {
        phase6_spin();
    }
    uint32_t generation;
    int32_t index = liberums_call(SYS_PAGE_CLAIM, RESOURCE_ALL, 0, 0, 0, &generation);
    check(index > 0, 2);
    check(call(SYS_PAGE_MAP, index, generation, RESOURCE_WINDOW, 3) == 0, 3);
    check(call(SYS_PAGE_MAP, index, generation, RESOURCE_WINDOW + 4096, 3) == 0, 4);
    for (uint32_t i = 0; i < 1024; ++i) {
        PAGE[i] = 0xa0000000u | i;
    }
    DATA[3] = index;
    DATA[4] = generation;
    DATA[5] = call(SYS_PAGE_QUERY, index, generation, 0, 0);
    DATA[2] = 1; /* Publish only after both real aliases exist. */
    phase6_user_checkpoint(1);
    if (DATA[1] == 3) {
        phase6_stale_write(); /* Ignores events and makes no further syscalls. */
    }
    uint32_t request;
    while (liberums_call(SYS_REVOKE_POLL, 0, 0, 0, 0, &request) == RES_ENOENT) {
        call(SYS_YIELD, 0, 0, 0, 0);
    }
    check(request != 0, 5);
    for (uint32_t field = 0; field < 4; ++field) {
        uint32_t value;
        check(liberums_call(SYS_REVOKE_INFO, request, field, 0, 0, &value) == 0, 6);
        DATA[12 + field] = value;
    }
    check(DATA[12] == (uint32_t)index && DATA[13] == generation && DATA[14] == REVOKE_REASON_TEST, 7);
    check(call(SYS_REVOKE_ACK, index, generation + 1, request, 0) == RES_ESTALE, 8);
    check(call(SYS_REVOKE_ACK, index, generation, request + 1, 0) == RES_ESTALE, 9);
    check(call(SYS_PAGE_MAP, index, generation, RESOURCE_WINDOW + 8192, 3) == RES_EBUSY, 10);
    check(call(SYS_PAGE_TRANSFER, index, generation, 2, RESOURCE_ALL) == RES_EBUSY, 11);
    if (DATA[1] == 5) {
        liberums_exit(0); /* Pending event cancelled on domain exit. */
    }
    if (DATA[1] == 4) {
        check(call(SYS_PAGE_RELEASE, index, generation, 0, 0) == 0, 12);
    } else {
        check(call(SYS_PAGE_UNMAP, RESOURCE_WINDOW, 0, 0, 0) == 0, 13);
        check(call(SYS_REVOKE_ACK, index, generation, request, 0) == 0, 14);
    }
    check(call(SYS_REVOKE_ACK, index, generation, request, 0) == RES_ESTALE, 15);
    check(call(SYS_PAGE_RELEASE, index, generation, 0, 0) == RES_ESTALE, 16);
    DATA[10] = 0xa600d;
    phase6_user_checkpoint(5);
    liberums_exit(0);
}
