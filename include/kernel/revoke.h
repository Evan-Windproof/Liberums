/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_REVOKE_H
#define LIBERUMS_REVOKE_H
#include <kernel/capability.h>
#include <abi/event.h>
struct domain;
struct revoke_event {
    bool active;
    struct resource_handle handle;
    uint32_t owner_id, owner_epoch;
    uint32_t reason, deadline, request_id;
};
/* Trusted policy hook, NOT a syscall granting users revocation authority. */
int32_t revoke_request(struct domain *owner, struct resource_handle handle,
                       uint32_t delay, uint32_t reason);
int32_t revoke_poll(struct domain *owner, uint32_t *request);
int32_t revoke_info(struct domain *owner, uint32_t request, uint32_t field, uint32_t *value);
int32_t revoke_ack(struct domain *owner, struct resource_handle handle, uint32_t request);
void revoke_expire(uint32_t now);
void revoke_cancel(struct domain *owner, struct resource_handle handle);
#endif
