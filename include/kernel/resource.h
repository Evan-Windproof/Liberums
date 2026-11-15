/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_RESOURCE_H
#define LIBERUMS_RESOURCE_H
#include <kernel/capability.h>
#define RESOURCE_CAPACITY 16u
#define RESOURCE_DOMAIN_LIMIT 4u
#define RESOURCE_BINDING_LIMIT 4u
struct domain;
enum resource_state { RESOURCE_FREE, RESOURCE_OWNED, RESOURCE_RECLAIMING };
struct resource_binding { uint32_t address, rights; };
struct resource {
    enum resource_state state;
    struct capability cap;
    uint32_t physical_address;
    uint32_t references;
    struct resource_binding bindings[RESOURCE_BINDING_LIMIT];
};
int32_t resource_claim(struct domain *owner, uint32_t rights, bool specific,
                       uint32_t pfn, struct resource_handle *handle);
int32_t resource_map(struct domain *owner, struct resource_handle handle,
                     uint32_t address, uint32_t rights);
int32_t resource_unmap(struct domain *owner, uint32_t address);
int32_t resource_release(struct domain *owner, struct resource_handle handle);
int32_t resource_query(struct domain *owner, struct resource_handle handle, uint32_t *rights);
int32_t resource_transfer(struct domain *owner, struct resource_handle handle,
                          uint32_t target_id, uint32_t rights);
int32_t resource_receive(struct domain *owner, struct resource_handle *handle);
void resource_cleanup(struct domain *owner);
#endif
