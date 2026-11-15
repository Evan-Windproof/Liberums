/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_TIMER_H
#define LIBERUMS_TIMER_H
#include <stdbool.h>
#include <stdint.h>
void timer_start(void);
void timer_stop(void);
bool timer_enabled(void);
bool timer_exhausted(void);
uint32_t timer_now(void);
uint32_t abi_features(void);
#endif
