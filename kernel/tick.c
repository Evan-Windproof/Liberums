/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/tick.h>
bool tick_advance(uint32_t *tick)
{
    if (*tick == UINT32_MAX) {
        return false;
    }
    ++*tick;
    return true;
}
bool tick_deadline(uint32_t now, uint32_t delay, uint32_t *deadline)
{
    if (delay == 0 || delay > UINT32_MAX - now) {
        return false;
    }
    *deadline = now + delay;
    return true;
}
