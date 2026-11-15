/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/syscall.h>
#include <kernel/domain.h>
#include <kernel/serial.h>
#include <kernel/user_abi.h>

void syscall_dispatch(struct exception_frame *frame)
{
    struct domain *domain = domain_from_trap(frame);
    ++domain->syscalls;
    switch (frame->eax) {
    case SYS_YIELD:
        frame->eax = 0;
        domain_yield(frame);
    case SYS_EXIT:
        domain_exit(frame, frame->ebx);
    case SYS_LOG_CHAR:
        if (frame->ebx > 255) {
            frame->eax = (uint32_t)-22; /* EINVAL; EBX is a scalar, never a pointer. */
        } else if (domain->log_count == 128) {
            frame->eax = (uint32_t)-11; /* Bounded diagnostic output per domain. */
        } else {
            char text[2] = {(char)frame->ebx, '\0'};
            serial_write(text);
            ++domain->log_count;
            frame->eax = 0;
        }
        break;
    default:
        frame->eax = (uint32_t)-38; /* ENOSYS, with no access to internal addresses. */
        break;
    }
    user_context_sanitize(frame);
}
