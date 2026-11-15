/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_TICK_H
#define LIBERUMS_TICK_H
#include <stdbool.h>
#include <stdint.h>
/* Boot-relative absolute ticks, NEVER modular time. Exhaustion fails closed. */
bool tick_advance(uint32_t *tick);
bool tick_deadline(uint32_t now, uint32_t delay, uint32_t *deadline);
#endif
