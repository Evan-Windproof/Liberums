/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_CAPABILITY_H
#define LIBERUMS_CAPABILITY_H
#include <stdbool.h>
#include <stdint.h>
#include <kernel/resource_abi.h>
struct capability {
    uint32_t generation; /* Full 32 bits, never wrap. */
    uint32_t owner_id, owner_epoch; /* Epoch distinguishes reused domain slots. */
    uint32_t rights;
};
bool capability_next(uint32_t current, uint32_t *next);
bool capability_rights_valid(uint32_t rights);
int32_t capability_check(const struct capability *cap, uint32_t generation,
                         uint32_t caller, uint32_t epoch, uint32_t required);
#endif
