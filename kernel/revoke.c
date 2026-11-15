/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/revoke.h>
#include <kernel/resource.h>
#include <kernel/domain.h>
#include <kernel/timer.h>
#include <kernel/tick.h>
#include <kernel/panic.h>
/* Capacity is a protocol bound, independent of the page being reclaimed. */
static struct revoke_event events[2 * REVOKE_EVENTS_PER_DOMAIN];
static uint32_t last_request_id;
static uint32_t acknowledged, forced, cancelled;
static bool for_owner(const struct revoke_event *event, const struct domain *owner)
{
    return event->active && event->owner_id == owner->id && event->owner_epoch == owner->epoch;
}
static struct revoke_event *find(struct domain *owner, uint32_t request)
{
    for (uint32_t i = 0; i < 2 * REVOKE_EVENTS_PER_DOMAIN; ++i) {
        if (for_owner(&events[i], owner) && events[i].request_id == request) {
            return &events[i];
        }
    }
    return NULL;
}
int32_t revoke_request(struct domain *owner, struct resource_handle handle,
                       uint32_t delay, uint32_t reason)
{
    if (!timer_enabled()) {
        return RES_EPROTONOSUPPORT;
    }
    if (owner == NULL || domain_lookup(owner->id) != owner
        || (reason != REVOKE_REASON_PRESSURE && reason != REVOKE_REASON_TEST)) {
        return RES_EINVAL;
    }
    uint32_t deadline;
    if (!tick_deadline(timer_now(), delay, &deadline)) {
        return RES_EOVERFLOW;
    }
    if (last_request_id == UINT32_MAX) {
        return RES_EOVERFLOW; /* IDs never wrap or alias an old ACK. */
    }
    uint32_t count = 0;
    struct revoke_event *slot = NULL;
    for (uint32_t i = 0; i < 2 * REVOKE_EVENTS_PER_DOMAIN; ++i) {
        if (!events[i].active) {
            slot = &events[i];
        } else if (for_owner(&events[i], owner)) {
            if (events[i].handle.index == handle.index
                && events[i].handle.generation == handle.generation) {
                return RES_EBUSY;
            }
            ++count;
        }
    }
    if (count == REVOKE_EVENTS_PER_DOMAIN || slot == NULL) {
        return RES_ENOSPC; /* No resource mutation and no notification overwrite. */
    }
    int32_t error = resource_revoke_begin(owner, handle);
    if (error != 0) {
        return error;
    }
    /* Single CPU, IF=0: publication and resource state form one commit. */
    *slot = (struct revoke_event){true, handle, owner->id, owner->epoch,
                                  reason, deadline, ++last_request_id};
    return 0;
}
int32_t revoke_poll(struct domain *owner, uint32_t *request)
{
    struct revoke_event *oldest = NULL;
    *request = 0;
    for (uint32_t i = 0; i < 2 * REVOKE_EVENTS_PER_DOMAIN; ++i) {
        if (for_owner(&events[i], owner)
            && (oldest == NULL || events[i].request_id < oldest->request_id)) {
            oldest = &events[i];
        }
    }
    if (oldest == NULL) {
        return RES_ENOENT;
    }
    *request = oldest->request_id;
    return 0;
}
int32_t revoke_info(struct domain *owner, uint32_t request, uint32_t field, uint32_t *value)
{
    *value = 0;
    struct revoke_event *event = find(owner, request);
    if (event == NULL) {
        return RES_ESTALE;
    }
    switch (field) {
    case REVOKE_INFO_INDEX: *value = event->handle.index; break;
    case REVOKE_INFO_GENERATION: *value = event->handle.generation; break;
    case REVOKE_INFO_REASON: *value = event->reason; break;
    case REVOKE_INFO_DEADLINE: *value = event->deadline; break;
    default: return RES_EINVAL;
    }
    return 0;
}
/* A checkpoint with actual state for the GDB audit; never fabricates success. */
__attribute__((noinline)) void revoke_completed(void)
{
    __asm__ volatile ("" : : : "memory");
}
static void complete(struct revoke_event *event, bool timeout)
{
    struct domain *owner = domain_slot(event->owner_id);
    if (owner == NULL || owner->epoch != event->owner_epoch) {
        panic("revocation owner lifetime mismatch");
    }
    event->active = false;
    if (resource_revoke_complete(owner, event->handle) != 0) {
        panic("revocation resource state mismatch");
    }
    if (timeout) {
        ++forced;
    } else {
        ++acknowledged;
    }
    revoke_completed();
}
int32_t revoke_ack(struct domain *owner, struct resource_handle handle, uint32_t request)
{
    struct revoke_event *event = find(owner, request);
    if (event == NULL || event->handle.index != handle.index
        || event->handle.generation != handle.generation) {
        return RES_ESTALE;
    }
    if (timer_now() >= event->deadline) {
        complete(event, true);
        return RES_ESTALE;
    }
    complete(event, false); /* ACK still revokes aliases, zeroes and frees in kernel. */
    return 0;
}
void revoke_expire(uint32_t now)
{
    for (uint32_t i = 0; i < 2 * REVOKE_EVENTS_PER_DOMAIN; ++i) {
        if (events[i].active && now >= events[i].deadline) {
            complete(&events[i], true);
        }
    }
}
void revoke_cancel(struct domain *owner, struct resource_handle handle)
{
    for (uint32_t i = 0; i < 2 * REVOKE_EVENTS_PER_DOMAIN; ++i) {
        struct revoke_event *event = &events[i];
        if (for_owner(event, owner) && event->handle.index == handle.index
            && event->handle.generation == handle.generation) {
            event->active = false;
            ++cancelled; /* Release or domain teardown supersedes notification. */
        }
    }
}
