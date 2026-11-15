/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_X86_GDT_H
#define LIBERUMS_X86_GDT_H
#include <stdint.h>
#define USER_CODE_SELECTOR 0x1b
#define USER_DATA_SELECTOR 0x23
void gdt_init(void);
void tss_set_kernel_stack(uint32_t top);
#endif
