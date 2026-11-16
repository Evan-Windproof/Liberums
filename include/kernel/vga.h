/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_KERNEL_VGA_H
#define LIBERUMS_KERNEL_VGA_H
#include <stdint.h>
#include <stdbool.h>
#define VGA_PHYSICAL 0x000b8000u
/* Page granularity: 4000 visible bytes plus 96 bytes in the VGA aperture. */
#define VGA_PAGE_BYTES 4096u
bool vga_probe(void); /* Boot only, before paging removes the firmware BDA. */
bool vga_available(void);
void vga_diagnostic(void);
bool vga_cursor_set(uint32_t cell);
#endif
