/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Frozen ABI 1.2 contract, intentionally independent of implementation tables. */
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <abi/elf.h>
#include <abi/vga.h>
#include <abi/keyboard.h>
#define ABI_VALUE(name, expected) _Static_assert((name)==(expected), #name " ABI drift")
ABI_VALUE(SYS_YIELD,0); ABI_VALUE(SYS_EXIT,1); ABI_VALUE(SYS_LOG_CHAR,2);
ABI_VALUE(SYS_ABI_QUERY,3); ABI_VALUE(SYS_RESOURCE_LIMITS,4);
ABI_VALUE(SYS_PAGE_CLAIM,16); ABI_VALUE(SYS_PAGE_SPECIFIC,17); ABI_VALUE(SYS_PAGE_MAP,18);
ABI_VALUE(SYS_PAGE_UNMAP,19); ABI_VALUE(SYS_PAGE_RELEASE,20); ABI_VALUE(SYS_PAGE_QUERY,21);
ABI_VALUE(SYS_PAGE_TRANSFER,22); ABI_VALUE(SYS_PAGE_RECEIVE,23);
ABI_VALUE(SYS_REVOKE_POLL,24); ABI_VALUE(SYS_REVOKE_INFO,25); ABI_VALUE(SYS_REVOKE_ACK,26);
ABI_VALUE(SYS_VGA_INFO,32); ABI_VALUE(SYS_VGA_ACQUIRE,33); ABI_VALUE(SYS_VGA_MAP,34);
ABI_VALUE(SYS_VGA_RELEASE,35); ABI_VALUE(SYS_VGA_CURSOR,36);
ABI_VALUE(SYS_KBD_INFO,40); ABI_VALUE(SYS_KBD_ACQUIRE,41); ABI_VALUE(SYS_KBD_POLL,42);
ABI_VALUE(SYS_KBD_RELEASE,43); ABI_VALUE(SYS_KBD_STATS,44);
ABI_VALUE(LIBERUMS_ABI_VERSION,0x00010002); ABI_VALUE(LIBERUMS_FEATURES,31);
ABI_VALUE(LIBERUMS_PREEMPT_FEATURES,119); ABI_VALUE(RESOURCE_ALL,63);
ABI_VALUE(RESOURCE_READ,1); ABI_VALUE(RESOURCE_WRITE,2); ABI_VALUE(RESOURCE_MAP,4);
ABI_VALUE(RESOURCE_RELEASE,8); ABI_VALUE(RESOURCE_TRANSFER,16); ABI_VALUE(RESOURCE_QUERY,32);
ABI_VALUE(LIBERUMS_RESOURCE_DOMAIN_LIMIT,4); ABI_VALUE(LIBERUMS_RESOURCE_BINDING_LIMIT,4);
ABI_VALUE(RESOURCE_WINDOW,0x09000000); ABI_VALUE(RESOURCE_WINDOW_END,0x09010000);
ABI_VALUE(VGA_DEVICE_HANDLE,0x10001); ABI_VALUE(KBD_HANDLE,0x10002);
ABI_VALUE(sizeof(struct resource_handle),8); ABI_VALUE(sizeof(struct liberums_abi_note),36);
ABI_VALUE(offsetof(struct liberums_abi_note,major),24);
ABI_VALUE(offsetof(struct liberums_abi_note,required_features),32);
int main(void)
{
    assert(liberums_abi_compatible(0x10002,31,1,1,29));
    assert(liberums_abi_compatible(0x10002,119,1,2,117));
    assert(!liberums_abi_compatible(0x10002,119,1,2,29)); /* No cooperative promise. */
    assert(!liberums_abi_compatible(0x10002,31,1,3,0));
    assert(!liberums_abi_compatible(0x10002,31,2,1,0));
    puts("PASS: frozen ABI 1.2: 26 syscall numbers, profiles, scalar layouts and minor/feature compatibility");
}
