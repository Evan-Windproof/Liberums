/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Trusted debugger mailbox, gated by an explicit boot option. No syscall, ELF
 * identity heuristic, user pointers, LibOS metadata or fabricated event state. */
#include <kernel/phase9_test.h>
#include <kernel/boot_info.h>
#include <kernel/domain.h>
#include <kernel/revoke.h>

static bool phase9_enabled;
static volatile struct {
    uint32_t submit, id, epoch, index, generation, delay;
    uint32_t completed, request, deadline;
    int32_t result;
} phase9_control;

__attribute__((noinline)) void phase9_request_done(void)
{
    __asm__ volatile ("nop" : : : "memory");
}
void phase9_test_init(const struct boot_context *boot)
{
    phase9_enabled = boot_has_option(boot->commandline, "selftest=phase9");
}
void phase9_test_step(struct domain *returned)
{
    if (!phase9_enabled || phase9_control.submit == 0) { return; }
    struct domain *owner = domain_lookup(phase9_control.id);
    int32_t error = 0;
    if (owner == NULL) { error = RES_ESRCH; }
    else if (owner->epoch != phase9_control.epoch) { error = RES_ESTALE; }
    else if (owner != returned) { return; } /* Wait for its safe slice boundary. */

    /* Consume once even on error. Old mailbox IDs cannot target a new epoch. */
    phase9_control.submit = 0;
    phase9_control.request = phase9_control.deadline = 0;
    if (error == 0) {
        struct resource_handle handle = {phase9_control.index, phase9_control.generation};
        error = revoke_request(owner, handle, phase9_control.delay, REVOKE_REASON_PRESSURE);
        if (error == 0) {
            uint32_t request = 0, deadline = 0;
            /* Non-consuming public protocol: report the oldest live request.
             * GDB also inspects the actual bounded event table by handle. */
            (void)revoke_poll(owner, &request);
            (void)revoke_info(owner, request, REVOKE_INFO_DEADLINE, &deadline);
            phase9_control.request = request;
            phase9_control.deadline = deadline;
        }
    }
    phase9_control.result = error;
    ++phase9_control.completed;
    phase9_request_done();
}
