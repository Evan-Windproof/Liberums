/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_KERNEL_SERIAL_H
#define LIBERUMS_KERNEL_SERIAL_H

#include <stdint.h>

void serial_init(void);
void serial_write(const char *text);
void serial_hex(uint32_t value);

#endif
