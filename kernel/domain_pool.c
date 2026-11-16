/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/domain_pool.h>
#include <kernel/elf_loader.h>
#include <kernel/device_grants.h>
#include <kernel/keyboard.h>
#include <kernel/vga_device.h>
#include <kernel/resource.h>
#include <kernel/revoke.h>
#include <kernel/timer.h>
#include <kernel/panic.h>

static struct domain *volatile pool_last;
static bool member(const struct domain *domain)
{
    for (uint32_t id = 1; id <= DOMAIN_CAPACITY; ++id) {
        if (domain_slot(id) == domain) { return true; }
    }
    return false;
}
static bool authority_held(const struct domain *domain)
{
    return domain->resource_count != 0 || domain->inbox.index != 0 || domain->inbox.generation != 0
        || resource_domain_busy(domain) || revoke_domain_busy(domain)
        || keyboard_domain_busy(domain) || vga_device_domain_busy(domain);
}
__attribute__((noinline)) void domain_pool_preparing(void) { __asm__ volatile ("nop" : : : "memory"); }
__attribute__((noinline)) void domain_pool_ready(void) { __asm__ volatile ("nop" : : : "memory"); }
__attribute__((noinline)) void domain_pool_reaped(void) { __asm__ volatile ("nop" : : : "memory"); }

int32_t domain_reserve(struct domain **result)
{
    if (result == NULL) { return RES_EINVAL; }
    *result = NULL;
    bool retired = false, busy = false;
    for (uint32_t id = 1; id <= DOMAIN_CAPACITY; ++id) {
        struct domain *slot = domain_slot(id);
        if (slot->state != DOMAIN_UNUSED) { continue; }
        if (authority_held(slot) || slot->space.live || slot->space.count != 0
            || slot->space.dynamic_table != 0 || slot->space.device_table != 0 || domain_is_current(slot)) {
            busy = true;
            continue; /* A dirty UNUSED object is not permission to reuse its identity. */
        }
        if (slot->epoch == UINT32_MAX) { retired = true; continue; }
        slot->id = id;
        slot->pool_owned = true;
        slot->state = DOMAIN_PREPARING; /* Not visible through domain_lookup yet. */
        *result = slot;
        pool_last = slot;
        domain_pool_preparing();
        return 0;
    }
    return busy ? RES_EBUSY : retired ? RES_EOVERFLOW : RES_ENOSPC;
}
void domain_cleanup(struct domain *domain)
{
    /* All callers run on a trusted kernel stack with IF=0, after switching CR3. */
    keyboard_revoke(domain);
    vga_device_revoke(domain);
    resource_cleanup(domain); /* Also cancels revocation events and clears inbox. */
}
int32_t domain_reap(struct domain *domain)
{
    if (!member(domain) || !domain->pool_owned || domain_is_current(domain)) { return RES_EINVAL; }
    if (domain->state != DOMAIN_EXITED && domain->state != DOMAIN_FAULTED
        && domain->state != DOMAIN_PREPARING) { return RES_EBUSY; }
    domain->last_state = domain->state;
    domain->state = DOMAIN_RECLAIMING;
    domain_cleanup(domain);
    if (authority_held(domain)) { panic("domain authority survived cleanup"); }
    address_space_destroy(&domain->space); /* Removes aliases, zeroes/frees only RAM. */
    uint8_t *context = (void *)&domain->context;
    for (size_t i = 0; i < sizeof(domain->context); ++i) { context[i] = 0; }
    uint8_t *segments = (void *)&domain->segments;
    for (size_t i = 0; i < sizeof(domain->segments); ++i) { segments[i] = 0; }
    for (size_t i = 0; i < sizeof(domain->kernel_stack); ++i) { domain->kernel_stack[i] = 0; }
    domain->last_kernel_frame = 0;
    domain->pool_owned = false;
    domain->state = DOMAIN_UNUSED; /* Publish reuse only after all cleanup succeeds. */
    pool_last = domain;
    domain_pool_reaped();
    return 0;
}
int32_t domain_create_external(const uint8_t *image, size_t length, uint32_t devices,
                               struct domain **result)
{
    if (result == NULL) { return RES_EINVAL; }
    *result = NULL;
    struct elf_plan plan;
    int32_t error = elf_validate_features(image, length, &plan, abi_features());
    if (error != 0) { return error; }
    struct domain *domain;
    error = domain_reserve(&domain);
    if (error != 0) { return error; }
    error = address_space_create_external(&domain->space, image, &plan);
    if (error == 0) {
        domain_initialize(domain, domain->id, plan.entry, LIBERUMS_STACK_TOP);
        error = device_grants_apply(domain, devices);
    }
    if (error != 0) {
        if (domain->state == DOMAIN_READY) { domain->state = DOMAIN_EXITED; }
        if (domain_reap(domain) != 0) { panic("failed domain rollback"); }
        return error;
    }
    *result = domain;
    pool_last = domain;
    domain_pool_ready();
    return 0;
}
