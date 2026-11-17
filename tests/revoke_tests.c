/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Protocol boundary tests execute real resource/revocation code, with bounded
 * fake physical memory and PTEs. Real CR3/TLB protection is tested in QEMU. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <kernel/domain.h>
#include <kernel/physical_pages.h>
#include <kernel/timer.h>
#include <kernel/panic.h>
#include "../kernel/resource.c"
#include "../kernel/revoke.c"
static struct domain owners[2];
static uint32_t backing[16][1024] __attribute__((aligned(4096)));
static bool allocated[16], no_memory, fail_bind;
static uint32_t ptes[2][16], now, unmaps;
static unsigned checks;
#define CHECK(x) do { assert(x); ++checks; } while (0)
_Noreturn void panic(const char *message) { fprintf(stderr, "%s\n", message); abort(); }
bool timer_enabled(void) { return true; }
uint32_t timer_now(void) { return now; }
struct domain *domain_slot(uint32_t id) { assert(id >= 1 && id <= 2); return &owners[id - 1]; }
struct domain *domain_lookup(uint32_t id)
{
    return id >= 1 && id <= 2 && owners[id - 1].state == DOMAIN_READY ? &owners[id - 1] : NULL;
}
static unsigned page_index(uint32_t address)
{
    assert(address >= (uintptr_t)backing && address < (uintptr_t)(backing + 16));
    assert((address & 4095) == 0);
    return (address - (uintptr_t)backing) / 4096;
}
enum physical_page_state physical_page_state(uint32_t address)
{
    return allocated[page_index(address)] ? PAGE_ALLOCATED : PAGE_FREE;
}
bool physical_page_alloc_specific(uint32_t address)
{
    unsigned i = page_index(address);
    if (no_memory || allocated[i]) { return false; }
    allocated[i] = true;
    return true;
}
bool physical_page_alloc(uint32_t *address)
{
    if (no_memory) { return false; }
    for (unsigned i = 0; i < 16; ++i) {
        if (!allocated[i]) {
            allocated[i] = true;
            *address = (uintptr_t)backing[i];
            return true;
        }
    }
    return false;
}
bool physical_page_free(uint32_t address)
{
    unsigned i = page_index(address);
    CHECK(allocated[i]);
    for (unsigned j = 0; j < 1024; ++j) { assert(backing[i][j] == 0); }
    allocated[i] = false;
    return true;
}
bool address_space_dynamic_address(uint32_t address)
{
    return address >= RESOURCE_WINDOW && address < RESOURCE_WINDOW_END && (address & 4095) == 0;
}
static unsigned owner_index(struct address_space *space)
{
    return space == &owners[0].space ? 0 : 1;
}
int32_t address_space_bind(struct address_space *space, uint32_t address, uint32_t physical, bool writable)
{
    (void)writable;
    if (fail_bind) { return RES_ENOMEM; }
    unsigned i = owner_index(space), j = (address - RESOURCE_WINDOW) / 4096;
    if (ptes[i][j]) { return RES_EBUSY; }
    ptes[i][j] = physical;
    return 0;
}
bool address_space_unbind(struct address_space *space, uint32_t address, uint32_t physical)
{
    unsigned i = owner_index(space), j = (address - RESOURCE_WINDOW) / 4096;
    CHECK(ptes[i][j] == physical);
    ptes[i][j] = 0;
    ++unmaps;
    return true;
}
static struct resource_handle claim(struct domain *owner)
{
    struct resource_handle h;
    CHECK(resource_claim(owner, RESOURCE_ALL, false, 0, &h) == 0);
    return h;
}
int main(void)
{
    owners[0].id = 1; owners[1].id = 2;
    owners[0].epoch = owners[1].epoch = 1;
    owners[0].state = owners[1].state = DOMAIN_READY;
    struct domain *a = &owners[0], *b = &owners[1];
    struct resource_handle h = claim(a);
    uint32_t address = resources[h.index - 1].physical_address, request, value;
    CHECK(address < PHYSICAL_LIMIT); /* -no-pie keeps fixture memory below 64 MiB. */
    CHECK(resource_map(a, h, RESOURCE_WINDOW, 3) == 0);
    CHECK(resource_map(a, h, RESOURCE_WINDOW + 4096, 3) == 0);
    backing[page_index(address)][5] = 0xdeadbeef;
    CHECK(revoke_request(a, h, 4, REVOKE_REASON_TEST) == 0);
    CHECK(revoke_poll(a, &request) == 0 && request == 1);
    CHECK(revoke_request(a, h, 4, REVOKE_REASON_TEST) == RES_EBUSY);
    CHECK(resource_map(a, h, RESOURCE_WINDOW + 8192, 3) == RES_EBUSY);
    CHECK(resource_transfer(a, h, 2, RESOURCE_ALL) == RES_EBUSY);
    CHECK(revoke_info(b, request, 0, &value) == RES_ESTALE);
    CHECK(revoke_ack(b, h, request) == RES_ESTALE);
    CHECK(revoke_ack(a, (struct resource_handle){h.index, h.generation + 1}, request) == RES_ESTALE);
    CHECK(revoke_ack(a, h, request + 1) == RES_ESTALE);
    CHECK(revoke_info(a, request, 99, &value) == RES_EINVAL);
    CHECK(revoke_info(a, request, REVOKE_INFO_DEADLINE, &value) == 0 && value == 4);
    a->epoch++;
    CHECK(revoke_ack(a, h, request) == RES_ESTALE);
    a->epoch--;
    now = 4;
    CHECK(revoke_ack(a, h, request) == RES_ESTALE); /* Exact deadline is expired. */
    CHECK(forced == 1 && acknowledged == 0 && unmaps == 2 && a->resource_count == 0);
    CHECK(revoke_poll(a, &value) == RES_ENOENT && value == 0);
    CHECK(revoke_ack(a, h, request) == RES_ESTALE);
    struct resource_handle replacement;
    CHECK(resource_claim(b, RESOURCE_ALL, true, address / 4096, &replacement) == 0);
    CHECK(replacement.index == h.index && replacement.generation > h.generation);
    CHECK(revoke_ack(a, h, request) == RES_ESTALE && b->resource_count == 1);
    CHECK(resource_release(b, replacement) == 0);
    h = claim(a);
    CHECK(revoke_request(a, h, 10, REVOKE_REASON_PRESSURE) == 0);
    CHECK(revoke_poll(a, &request) == 0);
    CHECK(revoke_ack(a, h, request) == 0 && acknowledged == 1);
    CHECK(revoke_ack(a, h, request) == RES_ESTALE);
    h = claim(a);
    CHECK(revoke_request(a, h, 10, REVOKE_REASON_TEST) == 0);
    CHECK(revoke_poll(a, &request) == 0);
    CHECK(resource_release(a, h) == 0 && cancelled == 1);
    CHECK(revoke_ack(a, h, request) == RES_ESTALE);
    h = claim(a);
    CHECK(resource_transfer(a, h, 2, RESOURCE_ALL) == 0);
    CHECK(revoke_ack(a, h, request) == RES_ESTALE);
    CHECK(resource_receive(b, &replacement) == 0);
    CHECK(resource_release(b, replacement) == 0);
    struct resource_handle many[3];
    for (unsigned i = 0; i < 3; ++i) { many[i] = claim(a); }
    CHECK(revoke_request(a, many[0], 10, REVOKE_REASON_TEST) == 0);
    CHECK(revoke_request(a, many[1], 10, REVOKE_REASON_TEST) == 0);
    CHECK(revoke_poll(a, &request) == 0);
    uint32_t saved_id = last_request_id;
    CHECK(revoke_request(a, many[2], 10, REVOKE_REASON_TEST) == RES_ENOSPC);
    CHECK(resources[many[2].index - 1].state == RESOURCE_OWNED && last_request_id == saved_id);
    CHECK(revoke_poll(a, &value) == 0 && value == request); /* Not overwritten. */
    resource_cleanup(a);
    CHECK(a->resource_count == 0 && revoke_poll(a, &value) == RES_ENOENT);
    revoke_expire(100); /* Cancelled requests must not double free. */
    h = claim(a);
    CHECK(revoke_request(a, h, 0, REVOKE_REASON_TEST) == RES_EOVERFLOW);
    now = UINT32_MAX - 1;
    CHECK(revoke_request(a, h, 2, REVOKE_REASON_TEST) == RES_EOVERFLOW);
    now = 0;
    last_request_id = UINT32_MAX - 1;
    CHECK(revoke_request(a, h, 1, REVOKE_REASON_TEST) == 0);
    CHECK(revoke_poll(a, &request) == 0 && request == UINT32_MAX);
    CHECK(revoke_ack(a, h, 0) == RES_ESTALE);
    CHECK(revoke_ack(a, h, request) == 0);
    h = claim(a);
    last_request_id = UINT32_MAX;
    CHECK(revoke_request(a, h, 1, REVOKE_REASON_TEST) == RES_EOVERFLOW);
    CHECK(resources[h.index - 1].state == RESOURCE_OWNED);
    CHECK(resource_release(a, h) == 0);
    no_memory = true;
    CHECK(resource_claim(a, RESOURCE_ALL, false, 0, &h) == RES_ENOMEM && a->resource_count == 0);
    no_memory = false;
    h = claim(a);
    fail_bind = true;
    CHECK(resource_map(a, h, RESOURCE_WINDOW, 3) == RES_ENOMEM);
    CHECK(resources[h.index - 1].references == 0 && ptes[0][0] == 0);
    fail_bind = false;
    CHECK(resource_release(a, h) == 0);
    now = 0; last_request_id = 100;
    h = claim(a);
    resources[h.index - 1].cap.generation = 0xffff0123u;
    h.generation = 0xffff0123u;
    CHECK(revoke_request(a, h, 1, REVOKE_REASON_TEST) == 0);
    CHECK(revoke_poll(a, &request) == 0);
    CHECK(revoke_ack(a, (struct resource_handle){h.index, 0x123u}, request) == RES_ESTALE);
    CHECK(revoke_ack(a, h, request) == 0);
    h = claim(a);
    CHECK(revoke_request(b, h, 1, REVOKE_REASON_TEST) == RES_EPERM);
    CHECK(revoke_request(a, h, 1, 99) == RES_EINVAL);
    CHECK(revoke_request(a, h, 1, REVOKE_REASON_TEST) == 0);
    a->state = DOMAIN_EXITED; /* Expiry racing terminal cleanup is still safe. */
    revoke_expire(1);
    resource_cleanup(a);
    CHECK(a->resource_count == 0);
    a->state = DOMAIN_READY;
    for (unsigned i = 0; i < 200; ++i) {
        h = claim(a);
        CHECK(revoke_request(a, h, 1, REVOKE_REASON_TEST) == 0);
        CHECK(revoke_poll(a, &request) == 0);
        CHECK(revoke_ack(a, h, request) == 0);
        CHECK(revoke_ack(a, h, request) == RES_ESTALE);
    }
    CHECK(a->resource_count == 0 && b->resource_count == 0);
    /* Adjacent ACK/deadline boundaries: the exact deadline belongs to force,
     * even if ACK reaches the kernel before the periodic expiry sweep. */
    h = claim(a);
    CHECK(revoke_request(a, h, 2, REVOKE_REASON_TEST) == 0);
    CHECK(revoke_poll(a, &request) == 0);
    CHECK(revoke_info(a, request, REVOKE_INFO_DEADLINE, &value) == 0);
    now = value - 1;
    uint32_t ack_before = acknowledged;
    CHECK(revoke_ack(a, h, request) == 0 && acknowledged == ack_before + 1);
    h = claim(a);
    CHECK(revoke_request(a, h, 1, REVOKE_REASON_TEST) == 0);
    CHECK(revoke_poll(a, &request) == 0);
    CHECK(revoke_info(a, request, REVOKE_INFO_DEADLINE, &value) == 0);
    now = value;
    uint32_t force_before = forced;
    CHECK(revoke_ack(a, h, request) == RES_ESTALE && forced == force_before + 1);
    CHECK(revoke_ack(a, h, request) == RES_ESTALE && forced == force_before + 1);
    CHECK(a->resource_count == 0);
    for (unsigned i = 0; i < 16; ++i) { CHECK(!allocated[i]); }
    printf("PASS: revocation protocol assertions=%u\n", checks);
}
