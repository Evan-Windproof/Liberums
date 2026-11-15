/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/syscall.h>
#include <kernel/domain.h>
#include <kernel/serial.h>
#include <kernel/user_abi.h>
#include <kernel/resource.h>

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
    case SYS_ABI_QUERY:
        frame->eax = LIBERUMS_ABI_VERSION;
        frame->edx = LIBERUMS_FEATURES;
        break;
    case SYS_PAGE_CLAIM:
    case SYS_PAGE_SPECIFIC:
    case SYS_PAGE_RECEIVE: {
        struct resource_handle handle;
        int32_t error;
        if (frame->eax == SYS_PAGE_RECEIVE) {
            error = resource_receive(domain, &handle);
        } else {
            bool specific = frame->eax == SYS_PAGE_SPECIFIC;
            error = resource_claim(domain, specific ? frame->ecx : frame->ebx,
                                    specific, frame->ebx, &handle);
        }
        frame->eax = error == 0 ? handle.index : (uint32_t)error;
        frame->edx = error == 0 ? handle.generation : 0;
        break;
    }
    case SYS_PAGE_MAP:
        frame->eax = resource_map(domain, (struct resource_handle){frame->ebx, frame->ecx},
                                  frame->edx, frame->esi);
        break;
    case SYS_PAGE_UNMAP:
        frame->eax = resource_unmap(domain, frame->ebx);
        break;
    case SYS_PAGE_RELEASE:
        frame->eax = resource_release(domain, (struct resource_handle){frame->ebx, frame->ecx});
        break;
    case SYS_PAGE_QUERY: {
        uint32_t rights = 0;
        frame->eax = resource_query(domain, (struct resource_handle){frame->ebx, frame->ecx}, &rights);
        frame->edx = rights;
        break;
    }
    case SYS_PAGE_TRANSFER:
        frame->eax = resource_transfer(domain, (struct resource_handle){frame->ebx, frame->ecx},
                                       frame->edx, frame->esi);
        break;
    default:
        frame->eax = (uint32_t)-38; /* ENOSYS, with no access to internal addresses. */
        break;
    }
    user_context_sanitize(frame);
}
