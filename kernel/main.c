/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <arch/x86/idt.h>
#include <arch/x86/paging.h>
#include <kernel/boot_info.h>
#include <kernel/panic.h>
#include <kernel/physical_pages.h>
#include <kernel/selftest.h>
#include <kernel/serial.h>
#include <kernel/domain.h>
#include <kernel/external.h>
#include <kernel/timer.h>
#include <kernel/phase6_test.h>
#include <kernel/vga.h>
#include <kernel/phase71_test.h>
#include <kernel/keyboard.h>
#include <kernel/phase72_test.h>
#include <kernel/external_launch.h>
#include <kernel/device_grants.h>
#include <kernel/diagnostics.h>

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
    struct external_config external = external_config_read(boot.commandline);
    diagnostics_init(boot.commandline);
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
    /* Probe firmware BDA before paging deliberately removes page zero. */
    if ((boot_has_option(boot.commandline, "vga-diag")
        || boot_has_option(boot.commandline, "vga-console")
        || boot_has_option(boot.commandline, "interactive-console")) && !vga_probe()) {
        panic("unsupported VGA boot mode");
    }
    /* An unavailable external device is reported by transactional launch,
     * after ELF validation, without granting or retaining an address space. */
    if (external.devices & DEVICE_VGA_READ) { (void)vga_probe(); }
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
    if (boot_has_option(boot.commandline, "vga-diag")) {
        vga_diagnostic();
        kernel_stop();
    }
    if (boot_has_option(boot.commandline, "preempt") || phase6_test_selected(boot.commandline)
        || boot_has_option(boot.commandline, "keyboard-diag")
        || boot_has_option(boot.commandline, "interactive-console") || external.preempt) {
        timer_start();
    }
    if (boot_has_option(boot.commandline, "keyboard-diag")) {
        if (!keyboard_start()) { panic("PS/2 initialization failed"); }
        keyboard_diagnostic();
    } else if (boot_has_option(boot.commandline, "interactive-console")) {
        phase72_run(boot.commandline);
    } else if (boot_has_option(boot.commandline, "vga-console")) {
        phase71_test_run(boot.commandline);
    } else if (phase6_test_selected(boot.commandline)) {
        phase6_test_run(boot.commandline);
    } else if (external.selected) {
        external_domains_run(&boot);
    } else {
        domains_run(boot.commandline);
    }
    timer_stop();
    kernel_stop();
}
