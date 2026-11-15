/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/resource.h>
#include <kernel/domain.h>
#include <kernel/physical_pages.h>
#include <kernel/panic.h>
#include <kernel/revoke.h>

static struct resource resources[RESOURCE_CAPACITY];

static bool owns(const struct resource *resource, const struct domain *owner)
{
    return (resource->state == RESOURCE_OWNED || resource->state == RESOURCE_REVOKE_PENDING)
        && resource->cap.owner_id == owner->id
        && resource->cap.owner_epoch == owner->epoch;
}

static int32_t resolve(struct domain *owner, struct resource_handle handle,
                       uint32_t rights, struct resource **result)
{
    if (handle.index == 0 || handle.index > RESOURCE_CAPACITY) {
        return RES_ESTALE;
    }
    struct resource *resource = &resources[handle.index - 1];
    if (resource->state != RESOURCE_OWNED && resource->state != RESOURCE_REVOKE_PENDING) {
        return RES_ESTALE;
    }
    int32_t error = capability_check(&resource->cap, handle.generation,
                                     owner->id, owner->epoch, rights);
    if (error == 0) {
        *result = resource;
    }
    return error;
}

static void zero_resource(uint32_t address)
{
    volatile uint32_t *page = (void *)(uintptr_t)address;
    for (uint32_t i = 0; i < PAGE_SIZE / sizeof(uint32_t); ++i) {
        page[i] = 0;
    }
}

static void forget_inbox(struct domain *owner, const struct resource *resource)
{
    uint32_t index = (uint32_t)(resource - resources) + 1;
    if (owner->inbox.index == index && owner->inbox.generation == resource->cap.generation) {
        owner->inbox = (struct resource_handle){0, 0};
    }
}

int32_t resource_claim(struct domain *owner, uint32_t rights, bool specific,
                       uint32_t pfn, struct resource_handle *handle)
{
    if (!capability_rights_valid(rights)) {
        return RES_EINVAL;
    }
    if (owner->resource_count == RESOURCE_DOMAIN_LIMIT) {
        return RES_ENOSPC;
    }
    uint32_t slot, generation = 0;
    for (slot = 0; slot < RESOURCE_CAPACITY; ++slot) {
        if (resources[slot].state == RESOURCE_FREE
            && capability_next(resources[slot].cap.generation, &generation)) {
            break;
        }
    }
    if (slot == RESOURCE_CAPACITY) {
        return RES_ENOSPC;
    }
    uint32_t address;
    if (specific) {
        if (pfn >= PHYSICAL_LIMIT / PAGE_SIZE) {
            return RES_EINVAL;
        }
        address = pfn * PAGE_SIZE;
        enum physical_page_state state = physical_page_state(address);
        if (state != PAGE_FREE) {
            return state == PAGE_ALLOCATED ? RES_EBUSY : RES_EPERM;
        }
        if (!physical_page_alloc_specific(address)) {
            panic("specific claim state mismatch");
        }
    } else if (!physical_page_alloc(&address)) {
        return RES_ENOMEM;
    }
    zero_resource(address);
    struct resource *resource = &resources[slot];
    resource->physical_address = address;
    resource->cap.generation = generation;
    resource->cap.owner_id = owner->id;
    resource->cap.owner_epoch = owner->epoch;
    resource->cap.rights = rights;
    resource->references = 0;
    resource->state = RESOURCE_OWNED; /* Publish only after allocation and zeroing. */
    ++owner->resource_count;
    handle->index = slot + 1;
    handle->generation = generation;
    return 0;
}

int32_t resource_map(struct domain *owner, struct resource_handle handle,
                     uint32_t address, uint32_t rights)
{
    struct resource *resource;
    int32_t error = resolve(owner, handle, RESOURCE_MAP, &resource);
    if (error != 0) {
        return error;
    }
    if (resource->state == RESOURCE_REVOKE_PENDING) {
        return RES_EBUSY;
    }
    if ((rights != RESOURCE_READ && rights != (RESOURCE_READ | RESOURCE_WRITE))
        || !address_space_dynamic_address(address)) {
        return RES_EINVAL;
    }
    if ((rights & resource->cap.rights) != rights) {
        return RES_EPERM;
    }
    uint32_t slot;
    for (slot = 0; slot < RESOURCE_BINDING_LIMIT; ++slot) {
        if (resource->bindings[slot].address == 0) {
            break;
        }
    }
    if (slot == RESOURCE_BINDING_LIMIT) {
        return RES_ENOSPC;
    }
    error = address_space_bind(&owner->space, address, resource->physical_address,
                               (rights & RESOURCE_WRITE) != 0);
    if (error != 0) {
        return error; /* No mapping record or reference was published. */
    }
    resource->bindings[slot] = (struct resource_binding){address, rights};
    ++resource->references;
    return 0;
}

static void revoke_binding(struct domain *owner, struct resource *resource, uint32_t slot)
{
    struct resource_binding *binding = &resource->bindings[slot];
    if (!address_space_unbind(&owner->space, binding->address, resource->physical_address)) {
        panic("resource binding/PTE mismatch");
    }
    binding->address = binding->rights = 0;
    --resource->references;
}

int32_t resource_unmap(struct domain *owner, uint32_t address)
{
    if (!address_space_dynamic_address(address)) {
        return RES_EINVAL;
    }
    for (uint32_t i = 0; i < RESOURCE_CAPACITY; ++i) {
        struct resource *resource = &resources[i];
        if (!owns(resource, owner)) {
            continue;
        }
        for (uint32_t j = 0; j < RESOURCE_BINDING_LIMIT; ++j) {
            if (resource->bindings[j].address == address) {
                revoke_binding(owner, resource, j);
                return 0;
            }
        }
    }
    return RES_ENOENT;
}

static void revoke_all(struct domain *owner, struct resource *resource)
{
    for (uint32_t i = 0; i < RESOURCE_BINDING_LIMIT; ++i) {
        if (resource->bindings[i].address != 0) {
            revoke_binding(owner, resource, i);
        }
    }
    if (resource->references != 0) {
        panic("resource reference leak");
    }
}

static void reclaim(struct domain *owner, struct resource *resource)
{
    revoke_cancel(owner, (struct resource_handle){(uint32_t)(resource - resources) + 1,
                                                  resource->cap.generation});
    forget_inbox(owner, resource);
    resource->state = RESOURCE_RECLAIMING;
    revoke_all(owner, resource);
    zero_resource(resource->physical_address);
    if (!physical_page_free(resource->physical_address)) {
        panic("resource physical state mismatch");
    }
    resource->state = RESOURCE_FREE;
    resource->cap.owner_id = resource->cap.owner_epoch = resource->cap.rights = 0;
    --owner->resource_count;
}

int32_t resource_release(struct domain *owner, struct resource_handle handle)
{
    struct resource *resource;
    int32_t error = resolve(owner, handle, RESOURCE_RELEASE, &resource);
    if (error == 0) {
        reclaim(owner, resource); /* Release explicitly revokes every alias. */
    }
    return error;
}

int32_t resource_query(struct domain *owner, struct resource_handle handle, uint32_t *rights)
{
    struct resource *resource;
    int32_t error = resolve(owner, handle, RESOURCE_QUERY, &resource);
    if (error != 0) {
        return error;
    }
    *rights = resource->cap.rights;
    return (int32_t)(resource->physical_address / PAGE_SIZE);
}

int32_t resource_transfer(struct domain *owner, struct resource_handle handle,
                          uint32_t target_id, uint32_t rights)
{
    struct resource *resource;
    int32_t error = resolve(owner, handle, RESOURCE_TRANSFER, &resource);
    if (error != 0) {
        return error;
    }
    if (resource->state == RESOURCE_REVOKE_PENDING) {
        return RES_EBUSY;
    }
    struct domain *target = domain_lookup(target_id);
    if (target == NULL) {
        return RES_ESRCH;
    }
    if (target == owner || !capability_rights_valid(rights)) {
        return RES_EINVAL;
    }
    if ((rights & resource->cap.rights) != rights) {
        return RES_EPERM;
    }
    if (target->resource_count == RESOURCE_DOMAIN_LIMIT) {
        return RES_ENOSPC;
    }
    if (target->inbox.index != 0) {
        return RES_EAGAIN;
    }
    uint32_t generation;
    if (!capability_next(resource->cap.generation, &generation)) {
        return RES_EOVERFLOW;
    }
    /* Commit is non-failing: single CPU, IF=0, owner is trapped in this call.
     * All rejecting conditions above leave owner, PTEs and bytes unchanged. */
    revoke_cancel(owner, (struct resource_handle){(uint32_t)(resource - resources) + 1,
                                                  resource->cap.generation});
    forget_inbox(owner, resource);
    resource->state = RESOURCE_RECLAIMING;
    revoke_all(owner, resource);
    zero_resource(resource->physical_address);
    resource->cap.generation = generation;
    resource->cap.owner_id = target->id;
    resource->cap.owner_epoch = target->epoch;
    resource->cap.rights = rights;
    --owner->resource_count;
    ++target->resource_count;
    resource->state = RESOURCE_OWNED;
    target->inbox = (struct resource_handle){handle.index, generation};
    return 0;
}

int32_t resource_receive(struct domain *owner, struct resource_handle *handle)
{
    struct resource *resource;
    if (owner->inbox.index == 0) {
        return RES_ENOENT;
    }
    *handle = owner->inbox;
    owner->inbox.index = owner->inbox.generation = 0;
    return resolve(owner, *handle, 0, &resource);
}

void resource_cleanup(struct domain *owner)
{
    for (uint32_t i = 0; i < RESOURCE_CAPACITY; ++i) {
        if (owns(&resources[i], owner)) {
            reclaim(owner, &resources[i]);
        }
    }
    owner->inbox = (struct resource_handle){0, 0};
    if (owner->resource_count != 0) {
        panic("domain resource count mismatch");
    }
}

int32_t resource_revoke_begin(struct domain *owner, struct resource_handle handle)
{
    struct resource *resource;
    int32_t error = resolve(owner, handle, 0, &resource);
    if (error != 0) {
        return error;
    }
    if (resource->state != RESOURCE_OWNED) {
        return RES_EBUSY;
    }
    resource->state = RESOURCE_REVOKE_PENDING;
    return 0;
}
int32_t resource_revoke_complete(struct domain *owner, struct resource_handle handle)
{
    struct resource *resource;
    int32_t error = resolve(owner, handle, 0, &resource);
    if (error != 0) {
        return error;
    }
    if (resource->state != RESOURCE_REVOKE_PENDING) {
        return RES_ESTALE;
    }
    reclaim(owner, resource);
    return 0;
}
