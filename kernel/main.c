/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <arch/x86/idt.h>
#include <arch/x86/paging.h>
#include <kernel/boot_info.h>
#include <kernel/panic.h>
#include <kernel/physical_pages.h>
#include <kernel/selftest.h>
#include <kernel/serial.h>
#include <kernel/domain.h>

static struct boot_context boot;
extern char __kernel_start[], __kernel_end[];

/* An observable runtime checkpoint: GDB stops here only after initialization. */
__attribute__((noinline)) void kernel_ready(void)
{
    serial_write("Phase 2: protected memory foundations ready\n");
}

_Noreturn void kernel_main(uint32_t boot_magic, uint32_t boot_info_address)
{
    serial_init();
    idt_init();
    serial_write("IDT: 32 exception gates ready\n");
    if (!boot_info_read(boot_magic, boot_info_address, &boot)) {
        panic("invalid or unsupported Multiboot memory information");
    }
    serial_write("Memory map: accepted regions=");
    serial_hex((uint32_t)boot.map.count);
    serial_write("\n");
    if (!physical_pages_init(&boot.map, boot.reserved, boot.reserved_count,
                             (uintptr_t)__kernel_start, (uintptr_t)__kernel_end)) {
        panic("physical page initialization failed");
    }
    serial_write("Physical pages: free=");
    serial_hex(physical_pages_free_count());
    serial_write("\n");
    if (!paging_init(&boot)) {
        panic("paging initialization failed");
    }
    uint32_t cr0;
    __asm__ volatile ("mov %%cr0, %0" : "=r"(cr0));
    if ((cr0 & UINT32_C(0x80010001)) != UINT32_C(0x80010001)
        || physical_pages_free_count() == 0) {
        panic("memory runtime checks failed");
    }
    serial_write("Paging: enabled with WP, cr0=");
    serial_hex(cr0);
    serial_write("\n");
    selftest_run(&boot);
    kernel_ready();
    /* Preserve the Phase 1 compatibility banner before the user-domain stage. */
    serial_write("Liberums: boot successful\n");
    serial_write("Phase 1: x86 32-bit C11 kernel, COM1 ready\n");
    serial_write("Multiboot handoff accepted; interrupts disabled; halting.\n");
    domains_run(boot.commandline);
    kernel_stop();
}
