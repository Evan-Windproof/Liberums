/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <arch/x86/pit.h>
#include <arch/x86/io.h>
void pit_init(void)
{
    outb(0x43, 0x34); /* Channel 0, low/high divisor, rate generator (mode 2). */
    outb(0x40, PIT_DIVISOR & 255);
    outb(0x40, PIT_DIVISOR >> 8);
}
