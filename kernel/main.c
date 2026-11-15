/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include <kernel/serial.h>

#define MULTIBOOT_BOOTLOADER_MAGIC UINT32_C(0x2badb002)

static _Noreturn void halt(void)
{
    for (;;) {
        __asm__ volatile ("cli; hlt" : : : "memory");
    }
}

_Noreturn void kernel_main(uint32_t boot_magic, uint32_t boot_info_address)
{
    serial_init();
    if (boot_magic != MULTIBOOT_BOOTLOADER_MAGIC || boot_info_address == 0) {
        serial_write("Liberums: invalid Multiboot handoff\n");
        halt();
    }

    /* Do not interpret or allocate from the boot memory map in Phase 1. */
    serial_write("Liberums: boot successful\n");
    serial_write("Phase 1: x86 32-bit C11 kernel, COM1 ready\n");
    serial_write("Multiboot handoff accepted; interrupts disabled; halting.\n");
    halt();
}
