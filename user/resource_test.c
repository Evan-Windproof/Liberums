/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "api.h"
#include <kernel/capability.h>

#define VA1 (RESOURCE_WINDOW + 0x3000)
#define VA2 (RESOURCE_WINDOW + 0x5000)
static volatile uint32_t *const results = (void *)(uintptr_t)USER_DATA;

static __attribute__((noinline)) void expect(int condition, uint32_t code)
{
    if (!condition) {
        user_exit(code);
    }
}

static __attribute__((noinline)) int32_t call(uint32_t number, uint32_t a, uint32_t b,
                                             uint32_t c, uint32_t d, uint32_t *extra)
{
    __asm__ volatile ("int $0x80" : "+a"(number), "+d"(c)
                      : "b"(a), "c"(b), "S"(d) : "memory", "cc");
    if (extra != 0) {
        *extra = c;
    }
    return (int32_t)number;
}

static struct resource_handle claim(uint32_t rights)
{
    uint32_t generation;
    int32_t index = call(SYS_PAGE_CLAIM, rights, 0, 0, 0, &generation);
    expect(index > 0, 100);
    return (struct resource_handle){(uint32_t)index, generation};
}
static int32_t operate(uint32_t op, struct resource_handle h, uint32_t c, uint32_t d)
{
    return call(op, h.index, h.generation, c, d, 0);
}
static uint32_t query(struct resource_handle h)
{
    int32_t pfn = operate(SYS_PAGE_QUERY, h, 0, 0);
    expect(pfn >= 0, 101);
    return (uint32_t)pfn;
}
static void map(struct resource_handle h, uint32_t va, uint32_t rights)
{
    expect(operate(SYS_PAGE_MAP, h, va, rights) == 0, 102);
}
static void release(struct resource_handle h)
{
    expect(operate(SYS_PAGE_RELEASE, h, 0, 0) == 0, 103);
}
static void zero_page(uint32_t va)
{
    volatile uint32_t *page = (void *)(uintptr_t)va;
    for (uint32_t i = 0; i < 1024; ++i) {
        expect(page[i] == 0, 104);
    }
}

/* These no-op user checkpoints expose state to GDB; they grant no authority. */
__attribute__((noinline)) void resource_checkpoint(uint32_t stage)
{
    results[10] = stage;
    __asm__ volatile (".global resource_checkpoint_observed; resource_checkpoint_observed: nop" : : : "memory");
}
static void checkpoint(uint32_t stage, struct resource_handle h, uint32_t pfn)
{
    results[11] = h.index;
    results[12] = h.generation;
    results[13] = pfn;
    resource_checkpoint(stage);
}

#if defined(RESOURCE_IMAGE_BASIC) || defined(RESOURCE_IMAGE_TRANSFER)
static void reject_foreign(struct resource_handle h, uint32_t pfn)
{
    expect(operate(SYS_PAGE_QUERY, h, 0, 0) == RES_EPERM, 110);
    expect(operate(SYS_PAGE_MAP, h, VA1, RESOURCE_READ) == RES_EPERM, 111);
    expect(call(SYS_PAGE_SPECIFIC, pfn, RESOURCE_ALL, 0, 0, 0) == RES_EBUSY, 112);
    expect(call(SYS_PAGE_UNMAP, VA1, 0, 0, 0, 0) == RES_ENOENT, 113);
}

#endif

#ifdef RESOURCE_IMAGE_BASIC
static void basic(uint32_t id)
{
    if (id == 2) {
        struct resource_handle foreign = {results[11], results[12]};
        reject_foreign(foreign, results[13]); /* GDB supplies a known hostile handle, not rights. */
        struct resource_handle own = claim(RESOURCE_ALL);
        uint32_t pfn = query(own);
        map(own, VA1, RESOURCE_READ | RESOURCE_WRITE);
        zero_page(VA1);
        *(volatile uint32_t *)VA1 = 0xbbbb1234;
        checkpoint(2, own, pfn);
        user_call(SYS_YIELD, 0);
        expect(*(volatile uint32_t *)VA1 == 0xbbbb1234, 114);
        expect(operate(SYS_PAGE_TRANSFER, own, 1, RESOURCE_ALL) == RES_ESRCH, 170);
        release(own);
        user_exit(0);
    }
    struct resource_handle h = claim(RESOURCE_ALL);
    uint32_t pfn = query(h);
    uint32_t reported_rights;
    expect(call(SYS_PAGE_QUERY, h.index, h.generation, 0, 0, &reported_rights) == (int32_t)pfn
           && reported_rights == RESOURCE_ALL, 179);
    if (results[7] > 1) {
        struct resource_handle previous_domain = {results[14], results[15]};
        expect(operate(SYS_PAGE_QUERY, previous_domain, 0, 0) == RES_ESTALE, 180);
    }
    map(h, VA1, RESOURCE_READ | RESOURCE_WRITE);
    map(h, VA2, RESOURCE_READ);
    zero_page(VA1);
    for (uint32_t i = 0; i < 1024; ++i) {
        ((volatile uint32_t *)VA1)[i] = 0xaaaa1234 ^ i;
    }
    expect(*(volatile uint32_t *)VA2 == 0xaaaa1234, 115);
    checkpoint(1, h, pfn);
    user_call(SYS_YIELD, 0);
    expect(*(volatile uint32_t *)VA1 == 0xaaaa1234, 116);
    expect(operate(SYS_PAGE_MAP, h, USER_CODE, RESOURCE_READ) == RES_EINVAL, 117);
    expect(operate(SYS_PAGE_MAP, h, 0x100000, RESOURCE_READ) == RES_EINVAL, 118);
    expect(operate(SYS_PAGE_MAP, h, VA1 + 1, RESOURCE_READ) == RES_EINVAL, 119);
    expect(operate(SYS_PAGE_MAP, h, RESOURCE_WINDOW_END, RESOURCE_READ) == RES_EINVAL, 120);
    expect(operate(SYS_PAGE_MAP, h, RESOURCE_WINDOW, 64) == RES_EINVAL, 121);
    expect(operate(SYS_PAGE_MAP, h, VA1, RESOURCE_READ) == RES_EBUSY, 122);
    expect(operate(SYS_PAGE_QUERY, (struct resource_handle){0, 0}, 0, 0) == RES_ESTALE, 181);
    expect(operate(SYS_PAGE_QUERY, (struct resource_handle){0xdeadbeef, 0xcafebabe}, 0, 0) == RES_ESTALE, 182);
    expect(operate(SYS_PAGE_QUERY, (struct resource_handle){h.index, 0}, 0, 0) == RES_ESTALE, 183);
    expect(operate(SYS_PAGE_QUERY, (struct resource_handle){999, 1}, 0, 0) == RES_ESTALE, 123);
    expect(operate(SYS_PAGE_QUERY, (struct resource_handle){h.index, h.generation + 1}, 0, 0) == RES_ESTALE, 124);
    expect(call(SYS_PAGE_CLAIM, 64, 0, 0, 0, 0) == RES_EINVAL, 125);
    expect(call(SYS_PAGE_SPECIFIC, 0, RESOURCE_ALL, 0, 0, 0) == RES_EPERM, 126);
    expect(call(SYS_PAGE_SPECIFIC, 0x100, RESOURCE_ALL, 0, 0, 0) == RES_EPERM, 127);
    expect(call(SYS_PAGE_SPECIFIC, 16384, RESOURCE_ALL, 0, 0, 0) == RES_EINVAL, 128);
    map(h, RESOURCE_WINDOW + 0x6000, RESOURCE_READ);
    map(h, RESOURCE_WINDOW + 0x7000, RESOURCE_READ);
    expect(operate(SYS_PAGE_MAP, h, RESOURCE_WINDOW + 0x8000, RESOURCE_READ) == RES_ENOSPC, 171);
    expect(call(SYS_PAGE_UNMAP, RESOURCE_WINDOW + 0x6000, 0, 0, 0, 0) == 0, 172);
    expect(call(SYS_PAGE_UNMAP, RESOURCE_WINDOW + 0x7000, 0, 0, 0, 0) == 0, 173);
    expect(call(SYS_PAGE_UNMAP, RESOURCE_WINDOW + 0x7000, 0, 0, 0, 0) == RES_ENOENT, 174);
    struct resource_handle extra[3];
    for (uint32_t i = 0; i < 3; ++i) {
        extra[i] = claim(RESOURCE_ALL);
    }
    expect(call(SYS_PAGE_CLAIM, RESOURCE_ALL, 0, 0, 0, 0) == RES_ENOSPC, 129);
    expect(operate(SYS_PAGE_MAP, extra[0], VA1, RESOURCE_READ) == RES_EBUSY, 130);
    for (uint32_t i = 0; i < 3; ++i) {
        release(extra[i]);
    }
    release(h); /* Must revoke both aliases. */
    expect(operate(SYS_PAGE_RELEASE, h, 0, 0) == RES_ESTALE, 131);
    checkpoint(3, h, pfn);
    uint32_t gen;
    uint32_t rights = RESOURCE_READ | RESOURCE_MAP | RESOURCE_RELEASE | RESOURCE_QUERY;
    int32_t index = call(SYS_PAGE_SPECIFIC, pfn, rights, 0, 0, &gen);
    expect(index > 0, 132);
    struct resource_handle replacement = {(uint32_t)index, gen};
    expect(operate(SYS_PAGE_QUERY, h, 0, 0) == RES_ESTALE, 133);
    expect(operate(SYS_PAGE_MAP, replacement, VA1, RESOURCE_READ | RESOURCE_WRITE) == RES_EPERM, 134);
    expect(operate(SYS_PAGE_TRANSFER, replacement, 2, RESOURCE_ALL) == RES_EPERM, 135);
    map(replacement, VA1, RESOURCE_READ);
    zero_page(VA1);
    checkpoint(4, replacement, query(replacement));
    release(replacement);
    for (uint32_t i = 0; i < 16; ++i) {
        struct resource_handle next = claim(RESOURCE_ALL);
        expect(operate(SYS_PAGE_QUERY, replacement, 0, 0) == RES_ESTALE, 136);
        release(next);
        replacement = next;
    }
    struct resource_handle attenuated = claim(RESOURCE_ALL & ~RESOURCE_WRITE);
    expect(operate(SYS_PAGE_TRANSFER, attenuated, 2, RESOURCE_ALL) == RES_EPERM, 175);
    release(attenuated);
    struct resource_handle limited = claim(RESOURCE_READ | RESOURCE_QUERY);
    expect(operate(SYS_PAGE_MAP, limited, VA1, RESOURCE_READ) == RES_EPERM, 137);
    expect(operate(SYS_PAGE_RELEASE, limited, 0, 0) == RES_EPERM, 138);
    user_exit(0); /* Exit must reclaim even rights-limited resources. */
}

#endif

#ifdef RESOURCE_IMAGE_TRANSFER
static void transfer(uint32_t id, uint32_t mode)
{
    if (id == 2) {
        struct resource_handle foreign = {results[11], results[12]};
        uint32_t pfn = results[13];
        reject_foreign(foreign, pfn);
        struct resource_handle held[4];
        for (uint32_t i = 0; i < 4; ++i) {
            held[i] = claim(RESOURCE_ALL);
        }
        checkpoint(2, held[0], query(held[0]));
        user_call(SYS_YIELD, 0);
        for (uint32_t i = 0; i < 4; ++i) {
            release(held[i]);
        }
        user_call(SYS_YIELD, 0);
        uint32_t generation;
        int32_t index = call(SYS_PAGE_RECEIVE, 0, 0, 0, 0, &generation);
        expect(index > 0 && generation != foreign.generation, 140);
        struct resource_handle received = {(uint32_t)index, generation};
        expect(query(received) == pfn, 141);
        expect(operate(SYS_PAGE_MAP, received, VA1, RESOURCE_READ | RESOURCE_WRITE) == RES_EPERM, 142);
        map(received, VA1, RESOURCE_READ);
        zero_page(VA1);
        checkpoint(5, received, pfn);
        expect(operate(SYS_PAGE_TRANSFER, received, 1, RESOURCE_ALL) == RES_EPERM, 143);
        release(received);
        expect(call(SYS_PAGE_RECEIVE, 0, 0, 0, 0, 0) == RES_ENOENT, 144);
        user_exit(0);
    }
    struct resource_handle h = claim(RESOURCE_ALL);
    uint32_t pfn = query(h);
    map(h, VA1, RESOURCE_READ | RESOURCE_WRITE);
    map(h, VA2, RESOURCE_READ | RESOURCE_WRITE);
    for (uint32_t i = 0; i < 1024; ++i) {
        ((volatile uint32_t *)VA1)[i] = 0xdeadbeef ^ i;
    }
    checkpoint(1, h, pfn);
    user_call(SYS_YIELD, 0);
    expect(operate(SYS_PAGE_TRANSFER, h, 2, RESOURCE_ALL) == RES_ENOSPC, 145);
    expect(*(volatile uint32_t *)VA2 == 0xdeadbeef && query(h) == pfn, 146);
    checkpoint(3, h, pfn);
    user_call(SYS_YIELD, 0);
    expect(operate(SYS_PAGE_TRANSFER, h, 99, RESOURCE_ALL) == RES_ESRCH, 147);
    expect(operate(SYS_PAGE_TRANSFER, h, 1, RESOURCE_ALL) == RES_EINVAL, 148);
    expect(operate(SYS_PAGE_TRANSFER, h, 2, 64) == RES_EINVAL, 176);
    uint32_t attenuated = RESOURCE_READ | RESOURCE_MAP | RESOURCE_RELEASE | RESOURCE_QUERY;
    expect(operate(SYS_PAGE_TRANSFER, h, 2, attenuated) == 0, 149);
    expect(operate(SYS_PAGE_QUERY, h, 0, 0) == RES_ESTALE, 150);
    expect(operate(SYS_PAGE_MAP, h, VA1, RESOURCE_READ) == RES_ESTALE, 151);
    expect(call(SYS_PAGE_UNMAP, VA2, 0, 0, 0, 0) == RES_ENOENT, 152);
    struct resource_handle second = claim(RESOURCE_ALL);
    expect(operate(SYS_PAGE_TRANSFER, second, 2, RESOURCE_ALL) == RES_EAGAIN, 153);
    release(second);
    checkpoint(4, h, pfn);
    if (mode == RESOURCE_TEST_TRANSFER_FAULT) {
        resource_fault_read(VA2);
        user_exit(154);
    }
    user_call(SYS_YIELD, 0);
    expect(operate(SYS_PAGE_RELEASE, h, 0, 0) == RES_ESTALE, 155);
    user_exit(0);
}

#endif

_Noreturn void resource_program(uint32_t id, uint32_t mode)
{
#ifdef RESOURCE_IMAGE_BASIC
    (void)mode;
    basic(id);
    user_exit(199);
#elif defined(RESOURCE_IMAGE_TRANSFER)
    transfer(id, mode);
    user_exit(199);
#else
    if (id == 2) {
        struct resource_handle own = claim(RESOURCE_ALL);
        map(own, VA1, RESOURCE_READ | RESOURCE_WRITE);
        zero_page(VA1);
        checkpoint(8, own, query(own));
        release(own);
        user_exit(0);
    }
    if (mode == RESOURCE_TEST_CLAIM_FAILURE) {
        uint32_t generation = 123;
        expect(call(SYS_PAGE_CLAIM, RESOURCE_ALL, 0, 0, 0, &generation) == RES_ENOMEM, 177);
        expect(generation == 0, 178);
        checkpoint(6, (struct resource_handle){0, 0}, 0);
        user_exit(0);
    }
    struct resource_handle h = claim(mode == RESOURCE_TEST_READONLY ?
        RESOURCE_READ | RESOURCE_MAP | RESOURCE_RELEASE | RESOURCE_QUERY : RESOURCE_ALL);
    uint32_t pfn = query(h);
    if (mode == RESOURCE_TEST_GENERATION) {
        if (results[7] == 1) {
            expect(h.index == 1 && h.generation == UINT32_MAX, 160);
            expect(operate(SYS_PAGE_TRANSFER, h, 2, RESOURCE_ALL) == RES_EOVERFLOW, 161);
        } else {
            expect(h.index != 1, 162);
        }
        release(h);
        struct resource_handle next = claim(RESOURCE_ALL);
        expect(next.index != 1, 163);
        expect(operate(SYS_PAGE_QUERY, h, 0, 0) == RES_ESTALE, 164);
        checkpoint(7, next, query(next));
        release(next);
        user_exit(0);
    }
    if (mode == RESOURCE_TEST_MAP_FAILURE) {
        expect(operate(SYS_PAGE_MAP, h, VA1, RESOURCE_READ) == RES_ENOMEM, 165);
        expect(query(h) == pfn, 166);
        release(h);
        checkpoint(6, h, pfn);
        user_exit(0);
    }
    map(h, VA1, mode == RESOURCE_TEST_READONLY ? RESOURCE_READ : RESOURCE_READ | RESOURCE_WRITE);
    zero_page(VA1);
    if (mode != RESOURCE_TEST_READONLY) {
        for (uint32_t i = 0; i < 1024; ++i) {
            ((volatile uint32_t *)VA1)[i] = 0x87654321 ^ i;
        }
        expect(*(volatile uint32_t *)VA1 == 0x87654321, 167);
    }
    if (mode == RESOURCE_TEST_EXIT) {
        checkpoint(6, h, pfn);
        user_exit(0); /* Intentionally leave the binding and resource live. */
    }
    if (mode == RESOURCE_TEST_UNMAP_FAULT) {
        map(h, VA2, RESOURCE_READ | RESOURCE_WRITE);
        expect(call(SYS_PAGE_UNMAP, VA1, 0, 0, 0, 0) == 0, 168);
        expect(*(volatile uint32_t *)VA2 == 0x87654321, 184);
    } else if (mode == RESOURCE_TEST_RELEASE_FAULT) {
        map(h, VA2, RESOURCE_READ | RESOURCE_WRITE);
        release(h);
    }
    checkpoint(6, h, pfn);
    if (mode == RESOURCE_TEST_READONLY) {
        resource_fault_write(VA1);
    } else {
        resource_fault_read(VA1);
    }
    user_exit(169);
#endif
}
