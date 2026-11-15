/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/domain.h>
#include <kernel/boot_info.h>
#include <kernel/panic.h>
#include <kernel/physical_pages.h>
#include <kernel/serial.h>
#include <kernel/user_abi.h>
#include <kernel/selftest.h>
#include <kernel/resource.h>
#include <kernel/resource_test.h>
#include <arch/x86/gdt.h>
#include <kernel/timer.h>

static struct domain domains[2];
static struct domain *current_domain;
static uint32_t free_before_domains, completed_rounds;
extern const uint8_t user_a_start[], user_a_end[], user_b_start[], user_b_end[];
extern const uint8_t resource_basic_start[], resource_basic_end[];
extern const uint8_t resource_transfer_start[], resource_transfer_end[];
extern const uint8_t resource_fault_start[], resource_fault_end[];

/* Test checkpoints deliberately contain no success logic. GDB inspects state here. */
__attribute__((noinline)) void domains_prepared(void)
{
    __asm__ volatile ("" : : : "memory");
}
__attribute__((noinline)) void domains_reaped(void)
{
    __asm__ volatile ("" : : : "memory");
}

struct domain *domain_lookup(uint32_t id)
{
    if (id < 1 || id > 2) {
        return NULL;
    }
    struct domain *domain = &domains[id - 1];
    return domain->state == DOMAIN_READY || domain->state == DOMAIN_RUNNING ? domain : NULL;
}

struct domain *domain_from_trap(struct exception_frame *frame)
{
    uint32_t cr3;
    __asm__ volatile ("mov %%cr3, %0" : "=r"(cr3));
    if (current_domain == NULL || current_domain->state != DOMAIN_RUNNING
        || frame->cs != USER_CODE_SELECTOR || cr3 != current_domain->space.pages[0]) {
        panic("invalid domain trap identity");
    }
    uintptr_t bottom = (uintptr_t)current_domain->kernel_stack;
    uintptr_t top = bottom + sizeof(current_domain->kernel_stack);
    if ((uintptr_t)frame < bottom || (uintptr_t)frame > top - sizeof(struct user_context)) {
        panic("domain trap outside trusted kernel stack");
    }
    const struct user_context *context = (const void *)frame;
    if (context->user_ss != USER_DATA_SELECTOR) {
        panic("unexpected user stack selector");
    }
    current_domain->last_kernel_frame = (uintptr_t)frame;
    return current_domain;
}

void user_context_sanitize(struct exception_frame *frame)
{
    /* Preserve arithmetic flags/DF; reject privileged/debug execution flags.
     * IF follows the explicit execution profile. Syscalls normalize segments;
     * asynchronous IRQs preserve their separately validated saved selectors. */
    frame->eflags = (frame->eflags & 0xcd5u) | 2 | (timer_enabled() ? 0x200u : 0);
}

_Noreturn void domain_yield(struct exception_frame *frame)
{
    struct domain *domain = domain_from_trap(frame);
    user_context_sanitize(frame);
    domain->context = *(const struct user_context *)(const void *)frame;
    domain->state = DOMAIN_READY;
    domain->segments = (struct segment_context){0x23, 0x23, 0x23, 0x23};
    ++domain->yields;
    arch_user_leave();
}

_Noreturn void domain_exit(struct exception_frame *frame, uint32_t status)
{
    struct domain *domain = domain_from_trap(frame);
    domain->exit_status = status;
    domain->state = DOMAIN_EXITED;
    arch_user_leave();
}

_Noreturn void domain_fault(struct exception_frame *frame, uint32_t address)
{
    struct domain *domain = domain_from_trap(frame);
    domain->context = *(const struct user_context *)(const void *)frame;
    domain->fault_vector = frame->vector;
    domain->fault_error = frame->error;
    domain->fault_eip = frame->eip;
    domain->fault_address = address;
    domain->state = DOMAIN_FAULTED;
    serial_write("\nUSER FAULT domain=");
    serial_hex(domain->id);
    serial_write(" vector=");
    serial_hex(frame->vector);
    serial_write(" error=");
    serial_hex(frame->error);
    serial_write(" eip=");
    serial_hex(frame->eip);
    serial_write(" cr2=");
    serial_hex(address);
    serial_write("\n");
    arch_user_leave(); /* Abandon the faulting instruction, never skip/retry it. */
}

struct domain *domain_slot(uint32_t id)
{
    if (id < 1 || id > 2) {
        panic("invalid internal domain slot");
    }
    return &domains[id - 1];
}

void domain_initialize(struct domain *domain, uint32_t id, uint32_t entry, uint32_t stack_top)
{
    if (domain->epoch == UINT32_MAX || domain->resource_count != 0 || domain->inbox.index != 0) {
        panic("domain epoch exhausted or resources leaked");
    }
    ++domain->epoch;
    domain->id = id;
    uint8_t *bytes = (void *)&domain->context;
    for (uint32_t i = 0; i < sizeof(domain->context); ++i) {
        bytes[i] = 0;
    }
    domain->context.cpu.eip = entry;
    domain->context.cpu.cs = USER_CODE_SELECTOR;
    domain->context.cpu.eflags = 2 | (timer_enabled() ? 0x200u : 0);
    domain->segments = (struct segment_context){0x23, 0x23, 0x23, 0x23};
    domain->preemptions = 0;
    domain->context.user_ss = USER_DATA_SELECTOR;
    domain->context.user_esp = stack_top - 4; /* i386 function-entry alignment. */
    domain->exit_status = domain->fault_vector = domain->fault_error = 0;
    domain->fault_eip = domain->fault_address = 0;
    domain->syscalls = domain->yields = domain->log_count = 0;
    domain->last_kernel_frame = 0;
    domain->state = DOMAIN_READY;
}

static bool prepare_domain(struct domain *domain, uint32_t id, enum user_test_mode mode, enum resource_test_mode resource_mode)
{
    const uint8_t *image = id == 1 ? user_a_start : user_b_start;
    uint32_t length = id == 1 ? (uintptr_t)user_a_end - (uintptr_t)image
                              : (uintptr_t)user_b_end - (uintptr_t)image;
    if (resource_mode != RESOURCE_TEST_NONE) {
        const uint8_t *end;
        if (resource_mode == RESOURCE_TEST_BASIC) {
            image = resource_basic_start;
            end = resource_basic_end;
        } else if (resource_mode == RESOURCE_TEST_TRANSFER || resource_mode == RESOURCE_TEST_TRANSFER_FAULT) {
            image = resource_transfer_start;
            end = resource_transfer_end;
        } else {
            image = resource_fault_start;
            end = resource_fault_end;
        }
        length = (uintptr_t)end - (uintptr_t)image;
    }
    uint32_t probe = id == 1 ? USER_PROBE_A : USER_PROBE_B;
    if (!address_space_create(&domain->space, image, length, probe)) {
        return false;
    }
    domain_initialize(domain, id, USER_CODE, USER_STACK_TOP);
    uint32_t *data = (void *)(uintptr_t)domain->space.pages[4];
    data[0] = id == 1 ? 0xaaaaaaaa : 0xbbbbbbbb;
    data[1] = mode;
    data[6] = resource_mode;
    data[7] = resource_mode != RESOURCE_TEST_NONE ? domain->epoch : 0;
    data[2] = id == 1 ? USER_PROBE_B : USER_PROBE_A;
    data[3] = probe;
    /* These scalar addresses are supplied only for controlled denial experiments. */
    data[4] = domain->space.pages[0];
    domain->state = DOMAIN_READY;
    return true;
}

bool domain_prepare_peer(struct domain *domain, uint32_t id)
{
    return prepare_domain(domain, id, USER_NORMAL, RESOURCE_TEST_NONE);
}

static enum user_test_mode selected_mode(const char *args)
{
    static const char *const options[] = {
        "domain-test=normal", "domain-test=kernel-read", "domain-test=kernel-write",
        "domain-test=cross-read", "domain-test=cross-write", "domain-test=cli",
        "domain-test=io", "domain-test=gate", "domain-test=table-write",
        "domain-test=physical-read", "domain-test=code-write", "domain-test=bad-stack",
        "domain-test=fpu"
    };
    for (uint32_t mode = 0; mode < sizeof(options) / sizeof(options[0]); ++mode) {
        if (boot_has_option(args, options[mode])) {
            return (enum user_test_mode)mode;
        }
    }
    return USER_NORMAL;
}

/* Only selectors a Ring 3 task could legitimately load in this flat GDT. */
static bool user_segment(uint32_t selector)
{
    /* Loading DS/ES/FS/GS checks max(CPL,RPL) against DPL. At CPL3 a
     * DPL3 descriptor may therefore legally be loaded with ANY RPL, not
     * just 3. Index zero is null regardless of its RPL. Preserve the bits. */
    uint32_t descriptor = selector & ~3u;
    return descriptor == 0 || descriptor == (USER_DATA_SELECTOR & ~3u)
        || descriptor == (USER_CODE_SELECTOR & ~3u);
}
_Noreturn void domain_preempt(struct exception_frame *frame, const struct segment_context *segments)
{
    struct domain *domain = domain_from_trap(frame);
    if (!user_segment(segments->ds) || !user_segment(segments->es)
        || !user_segment(segments->fs) || !user_segment(segments->gs)) {
        panic("unsafe saved user segment");
    }
    user_context_sanitize(frame);
    domain->context = *(const struct user_context *)(const void *)frame;
    domain->segments = *segments;
    domain->state = DOMAIN_READY;
    ++domain->preemptions;
    arch_user_leave();
}
void domain_run_slice(struct domain *domain)
{
    current_domain = domain;
    domain->state = DOMAIN_RUNNING;
    tss_set_kernel_stack((uintptr_t)domain->kernel_stack + sizeof(domain->kernel_stack));
    address_space_activate(&domain->space);
    arch_user_enter(&domain->context, &domain->segments);
    /* This is the boot stack, with IF=0. Reclaim never runs on a user's trap stack. */
    address_space_kernel();
    current_domain = NULL;
}

void domains_run(const char *commandline)
{
    gdt_init();
    if (boot_has_option(commandline, "domain-test=allocation-failure")) {
        selftest_address_space_failure();
    }
    enum user_test_mode mode = selected_mode(commandline);
    enum resource_test_mode resource_mode = resource_test_select(commandline);
    bool reverse = boot_has_option(commandline, "domain-reverse");
    /* Recreate both domains twice, exercising teardown, zeroing and reuse. */
    for (uint32_t round = 0; round < 2; ++round) {
        free_before_domains = physical_pages_free_count();
        if (!prepare_domain(&domains[0], 1, reverse ? USER_NORMAL : mode, resource_mode)) {
            panic("first domain creation failed");
        }
        if (!prepare_domain(&domains[1], 2, reverse ? mode : USER_NORMAL, resource_mode)) {
            address_space_destroy(&domains[0].space);
            domains[0].state = DOMAIN_UNUSED;
            panic("second domain creation failed");
        }
        uint32_t *a_data = (void *)(uintptr_t)domains[0].space.pages[4];
        uint32_t *b_data = (void *)(uintptr_t)domains[1].space.pages[4];
        a_data[5] = domains[1].space.pages[4];
        b_data[5] = domains[0].space.pages[4];
        domains_prepared();
        resource_test_pressure_begin(resource_mode);
        domain_schedule();
        for (uint32_t i = 0; i < 2; ++i) {
            if (domains[i].state != DOMAIN_EXITED && domains[i].state != DOMAIN_FAULTED) {
                panic("domain scheduler state corruption");
            }
            address_space_destroy(&domains[i].space);
        }
        if (physical_pages_free_count() != free_before_domains) {
            panic("domain physical page leak");
        }
        ++completed_rounds;
        domains_reaped();
        serial_write("\nDomains: round completed, all pages reclaimed\n");
    }
    serial_write("Phase 3: user domains completed\n");
    if (resource_mode != RESOURCE_TEST_NONE) {
        serial_write("Phase 4: resource experiment completed\n");
    }
}
