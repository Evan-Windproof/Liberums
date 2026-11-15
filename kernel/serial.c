/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <arch/x86/io.h>
#include <kernel/serial.h>

#define COM1 0x3f8

void serial_init(void)
{
    outb(COM1 + 1, 0x00); /* Disable UART interrupts. */
    outb(COM1 + 3, 0x80); /* Enable divisor latch. */
    outb(COM1 + 0, 0x01); /* 115200 baud (divisor 1). */
    outb(COM1 + 1, 0x00);
    outb(COM1 + 3, 0x03); /* Eight data bits, no parity, one stop bit. */
    outb(COM1 + 2, 0xc7); /* Enable and clear FIFO, 14-byte receive trigger. */
    outb(COM1 + 4, 0x0b); /* DTR, RTS, OUT2; no loopback. */
}

static void serial_putc(char character)
{
    while ((inb(COM1 + 5) & 0x20) == 0) {
        /* Early boot diagnostic console: polling, no scheduler or IRQs. */
    }
    outb(COM1, (uint8_t)character);
}

void serial_write(const char *text)
{
    while (*text != '\0') {
        if (*text == '\n') {
            serial_putc('\r');
        }
        serial_putc(*text++);
    }
}
