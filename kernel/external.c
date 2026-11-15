/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/external.h>
#include <kernel/elf_loader.h>
#include <kernel/domain.h>
#include <kernel/physical_pages.h>
#include <kernel/panic.h>
#include <kernel/serial.h>
#include <arch/x86/gdt.h>

static struct elf_plan external_plan;
static int32_t external_load_result;
static uint32_t external_free_before, external_free_before_load, external_free_after_load;
static uint32_t external_completed_rounds;
__attribute__((noinline)) void external_prepared(void)
{
    __asm__ volatile ("" : : : "memory");
}
__attribute__((noinline)) void external_reaped(void)
{
    __asm__ volatile ("" : : : "memory");
}

void external_domains_run(const struct boot_context *boot)
{
    const uint8_t *image = NULL;
    int32_t validation = RES_ENOEXEC;
    if (boot->module_count == 1) {
        image = (const void *)(uintptr_t)boot->modules[0].begin;
        validation = elf_validate(image, boot->modules[0].end - boot->modules[0].begin, &external_plan);
    }
    gdt_init();
    for (uint32_t round = 0; round < 2; ++round) {
        struct domain *program = domain_slot(1);
        struct domain *peer = domain_slot(2);
        external_free_before = physical_pages_free_count();
        program->state = DOMAIN_UNUSED;
        if (!domain_prepare_peer(peer, 2)) {
            panic("external peer creation failed");
        }
        elf_test_pressure_begin(boot_has_option(boot->commandline, "external-oom"));
        external_free_before_load = physical_pages_free_count();
        external_load_result = validation;
        if (validation == 0) {
            external_load_result = address_space_create_external(&program->space, image, &external_plan);
            if (external_load_result == 0) {
                domain_initialize(program, 1, external_plan.entry, LIBERUMS_STACK_TOP);
            }
        }
        external_free_after_load = physical_pages_free_count();
        elf_test_pressure_end();
        if (external_load_result != 0) {
            if (external_free_before_load != external_free_after_load) {
                panic("external load rollback leaked pages");
            }
            serial_write("External ELF rejected: ");
            serial_hex((uint32_t)external_load_result);
            serial_write("\n");
        }
        external_prepared();
        domain_schedule();
        for (uint32_t id = 1; id <= 2; ++id) {
            struct domain *domain = domain_slot(id);
            if (domain->space.live) {
                if (domain->state != DOMAIN_EXITED && domain->state != DOMAIN_FAULTED) {
                    panic("external domain not terminal");
                }
                address_space_destroy(&domain->space);
            }
        }
        if (physical_pages_free_count() != external_free_before) {
            panic("external lifetime page leak");
        }
        ++external_completed_rounds;
        external_reaped();
    }
    serial_write("Phase 5: external execution completed\n");
}
