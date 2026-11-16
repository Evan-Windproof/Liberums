/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <arch/x86/pic.h>
#include <arch/x86/io.h>
/* ICW writes require an I/O delay on older PC hardware. */
static void write_wait(unsigned short port, unsigned char value)
{
    outb(port, value);
    outb(0x80, 0);
}
void pic_mask_all(void)
{
    outb(0x21, 0xff);
    outb(0xa1, 0xff);
}
void pic_init_timer(void)
{
    pic_mask_all();
    write_wait(0x20, 0x11);
    write_wait(0xa0, 0x11);
    write_wait(0x21, 0x20); /* IRQ0..7 -> vectors 32..39. */
    write_wait(0xa1, 0x28); /* IRQ8..15 -> vectors 40..47. */
    write_wait(0x21, 4);
    write_wait(0xa1, 2);
    write_wait(0x21, 1); /* 8086 mode, explicit EOI. */
    write_wait(0xa1, 1);
    outb(0xa1, 0xff);
    outb(0x21, 0xfe); /* Only IRQ0; even the slave cascade remains masked. */
}
void pic_timer_eoi(void)
{
    outb(0x20, 0x20); /* IRQ0 belongs only to the master. */
}

/* Caller has IF=0. Preserve IRQ0 and every other mask bit. */
void pic_keyboard_mask(bool masked)
{
    uint8_t mask = inb(0x21);
    outb(0x21, masked ? mask | 2u : mask & ~2u);
}
void pic_keyboard_eoi(void)
{
    outb(0x20, 0x20); /* IRQ1 is also on the master; no slave EOI. */
}
