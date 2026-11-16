/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/multi_external.h>
#include <kernel/domain_pool.h>
#include <kernel/device_grants.h>
#include <kernel/physical_pages.h>
#include <kernel/serial.h>
#include <kernel/panic.h>
#include <kernel/phase8_test.h>
#include <arch/x86/gdt.h>
/* Records bind a module ordinal to the actual allocated lifetime, never a filename. */
static struct {
    int32_t result;
    uint32_t id, epoch;
} multi_loads[BOOT_MODULE_MAX];
static uint32_t multi_free_before, multi_completed;
__attribute__((noinline)) void multi_prepared(void) { __asm__ volatile ("nop" : : : "memory"); }
__attribute__((noinline)) void multi_reaped(void) { __asm__ volatile ("nop" : : : "memory"); }

uint32_t multi_module_devices(const char *args, uint32_t module)
{
    if (module == 0 || module > BOOT_MODULE_MAX) { return 0; }
    char vga[] = "multi-vga=1", readonly[] = "multi-vga-readonly=1", keyboard[] = "multi-keyboard=1";
    vga[sizeof(vga)-2] = readonly[sizeof(readonly)-2] = keyboard[sizeof(keyboard)-2] = (char)('0'+module);
    uint32_t devices = boot_has_option(args,vga) ? DEVICE_VGA_READ | DEVICE_VGA_WRITE : 0;
    if (boot_has_option(args,readonly)) { devices = DEVICE_VGA_READ; }
    if (boot_has_option(args,keyboard)) { devices |= DEVICE_KEYBOARD; }
    return devices;
}
void multi_domains_run(const struct boot_context *boot)
{
    gdt_init();
    multi_free_before = physical_pages_free_count();
    for (uint32_t i = 0; i < boot->module_count; ++i) {
        const struct physical_range *module = &boot->modules[i];
        struct domain *domain = NULL;
        phase8_test_before_module(i+1);
        int32_t error = domain_create_external((const void *)(uintptr_t)module->begin,
            module->end - module->begin, multi_module_devices(boot->commandline,i+1), &domain);
        phase8_test_after_module();
        multi_loads[i].result = error;
        if (error == 0) { multi_loads[i].id = domain->id; multi_loads[i].epoch = domain->epoch; }
        serial_write("Multi ELF module="); serial_hex(i+1);
        serial_write(" result="); serial_hex((uint32_t)error);
        serial_write(" id="); serial_hex(multi_loads[i].id);
        serial_write(" epoch="); serial_hex(multi_loads[i].epoch); serial_write("\n");
    }
    multi_prepared();
    domain_schedule();
    for (uint32_t id = 1; id <= DOMAIN_CAPACITY; ++id) {
        struct domain *domain = domain_slot(id);
        if (!domain->pool_owned) { continue; }
        serial_write("Multi terminal id="); serial_hex(id);
        serial_write(" epoch="); serial_hex(domain->epoch);
        serial_write(" state="); serial_hex(domain->state);
        serial_write(" status="); serial_hex(domain->exit_status); serial_write("\n");
        if (domain_reap(domain) != 0) { panic("multi domain reap failed"); }
        ++multi_completed;
    }
    if (physical_pages_free_count() != multi_free_before) { panic("multi domain lifetime leak"); }
    multi_reaped();
    serial_write("Phase 8: multi external execution completed\n");
}
