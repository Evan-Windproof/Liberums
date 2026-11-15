/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/panic.h>
#include <kernel/serial.h>

_Noreturn void kernel_stop(void)
{
    for (;;) {
        __asm__ volatile ("cli; hlt" : : : "memory");
    }
}

_Noreturn void panic(const char *reason)
{
    serial_write("PANIC: ");
    serial_write(reason);
    serial_write("\nHALTED\n");
    kernel_stop();
}
