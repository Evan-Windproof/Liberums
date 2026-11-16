/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_PS2_H
#define LIBERUMS_PS2_H
#include <stdbool.h>
#include <stdint.h>
enum ps2_result { PS2_EMPTY, PS2_DATA, PS2_DISCARD };
bool ps2_start(void);
void ps2_stop(void);
enum ps2_result ps2_read(uint8_t *byte);
#endif
