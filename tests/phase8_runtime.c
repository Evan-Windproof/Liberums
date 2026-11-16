/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Explicit trusted diagnostics only. No application name/address/heap policy. */
#include <kernel/phase8_test.h>
#include <kernel/domain_pool.h>
#include <kernel/boot_info.h>
#include <kernel/revoke.h>
#include <kernel/physical_pages.h>
#include <kernel/panic.h>
#include <kernel/external.h>
static const struct boot_context *test_boot;
static bool enabled;
static uint32_t pressure_module;
static volatile struct {
    uint32_t reuse_id,reuse_module,reuse_devices,reuse_done,old_epoch,new_epoch;
    uint32_t revoke_id,index,generation,delay,request,free_before,free_after;
    int32_t result;
} phase8_control;
__attribute__((noinline)) void phase8_action_done(void) { __asm__ volatile ("nop" : : : "memory"); }
void phase8_test_init(const struct boot_context *boot)
{
    test_boot=boot;
    enabled=boot_has_option(boot->commandline,"selftest=phase8");
    for (uint32_t i=1; enabled && i<=BOOT_MODULE_MAX; ++i) {
        char option[]="phase8-oom=1";
        option[sizeof(option)-2]=(char)('0'+i);
        if (boot_has_option(boot->commandline,option)) { pressure_module=i; }
    }
}
void phase8_test_before_module(unsigned int ordinal)
{
    /* Hold real free RAM, leaving five pages: exercise partial ELF rollback. */
    if (enabled && ordinal==pressure_module) { elf_test_pressure_begin(true); }
}
void phase8_test_after_module(void) { if (enabled) { elf_test_pressure_end(); } }
void phase8_test_step(struct domain *domain)
{
    if (!enabled) { return; }
    if (phase8_control.revoke_id==domain->id && domain->state==DOMAIN_READY) {
        phase8_control.result=revoke_request(domain,(struct resource_handle){phase8_control.index,phase8_control.generation},
            phase8_control.delay,REVOKE_REASON_TEST);
        phase8_control.request=0;
        if (phase8_control.result==0) { (void)revoke_poll(domain,(uint32_t *)&phase8_control.request); }
        phase8_control.revoke_id=0;
        phase8_action_done();
    }
    if (phase8_control.reuse_id==domain->id && domain->pool_owned
        && (domain->state==DOMAIN_EXITED || domain->state==DOMAIN_FAULTED)) {
        uint32_t module=phase8_control.reuse_module;
        phase8_control.reuse_id=0;
        phase8_control.old_epoch=domain->epoch;
        phase8_control.free_before=physical_pages_free_count();
        phase8_control.result=domain_reap(domain);
        if (phase8_control.result==0 && module<test_boot->module_count) {
            const struct physical_range *image=&test_boot->modules[module];
            struct domain *created=NULL;
            phase8_control.result=domain_create_external((const void *)(uintptr_t)image->begin,image->end-image->begin,
                phase8_control.reuse_devices,&created);
            if (created!=NULL) { phase8_control.new_epoch=created->epoch; }
        }
        phase8_control.free_after=physical_pages_free_count();
        ++phase8_control.reuse_done;
        phase8_action_done();
    }
}
