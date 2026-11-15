/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_KERNEL_SERIAL_H
#define LIBERUMS_KERNEL_SERIAL_H

void serial_init(void);
void serial_write(const char *text);

#endif
