/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_KERNEL_DOMAIN_H
#define LIBERUMS_KERNEL_DOMAIN_H
#include <arch/x86/user_context.h>
#include <arch/x86/paging.h>
#include <kernel/capability.h>
#include <arch/x86/irq.h>

enum domain_state { DOMAIN_UNUSED, DOMAIN_READY, DOMAIN_RUNNING, DOMAIN_EXITED, DOMAIN_FAULTED };
struct domain {
    uint32_t id, epoch;
    uint32_t resource_count;
    struct resource_handle inbox;
    enum domain_state state;
    struct address_space space;
    struct user_context context; /* Saved registers and complete outer-level iret frame. */
    struct segment_context segments;
    uint32_t preemptions;
    uint32_t exit_status;
    uint32_t fault_vector, fault_error, fault_eip, fault_address;
    uint32_t syscalls, yields, log_count;
    uint32_t last_kernel_frame; /* Diagnostic address, always on this domain's trusted stack. */
    uint8_t kernel_stack[8192] __attribute__((aligned(16)));
};
struct domain *domain_lookup(uint32_t id);
struct domain *domain_slot(uint32_t id);
void domain_initialize(struct domain *domain, uint32_t id, uint32_t entry, uint32_t stack_top);
bool domain_prepare_peer(struct domain *domain, uint32_t id);
void domain_schedule(void);
void domain_run_slice(struct domain *domain);
_Noreturn void domain_preempt(struct exception_frame *frame, const struct segment_context *segments);
void domains_run(const char *commandline);
struct domain *domain_from_trap(struct exception_frame *frame);
_Noreturn void domain_yield(struct exception_frame *frame);
_Noreturn void domain_exit(struct exception_frame *frame, uint32_t status);
_Noreturn void domain_fault(struct exception_frame *frame, uint32_t address);
void user_context_sanitize(struct exception_frame *frame);
#endif
