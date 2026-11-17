/* SPDX-License-Identifier: GPL-3.0-or-later */
/* External application using only the exported, opaque Aelthuvor App SDK and
 * public kernel ABI. No LibOS metadata is inspected or patched by this test. */
#include <aelthuvor/app.h>
#include <abi/syscall.h>
volatile uint32_t ram_command, ram_stage, ram_sequence, ram_address;
volatile uint32_t ram_index, ram_generation, ram_request, ram_pfn;
volatile int32_t ram_result;
__attribute__((noinline)) static void observed(uint32_t stage)
{
    ram_stage=stage; ++ram_sequence;
    __asm__ volatile(".global ram_observed; ram_observed: nop" : : : "memory");
}
__attribute__((noinline)) static void idle(void)
{
    __asm__ volatile(".global ram_idle; ram_idle: nop" : : : "memory");
    liberums_call(SYS_YIELD,0,0,0,0,0);
}
int ael_app_main(struct ael_app_context *context)
{
    unsigned char *allocation=ael_app_alloc(context,128);
    if (!allocation) { return 70; }
    ram_address=(uintptr_t)allocation;
    for (unsigned i=0;i<128;++i) { allocation[i]=(unsigned char)(i^0x9a); }
    observed(1);
    for (;;) {
        uint32_t command=ram_command; ram_command=0;
        if (!command) { idle();continue; }
        if (command==1) { /* Empty arena retained, so the LibOS can ACK it. */
            ram_result=ael_app_free(context,allocation);allocation=0;
            if (ram_result) { return 71; }
            observed(2);
        } else if (command==2) {
            ram_result=ael_app_check(context); /* Unmodified LibOS POLL/INFO/ACK. */
            observed(3); /* Unsafe active/lost memory must exit 96 before here. */
        } else if (command==3) {
            __asm__ volatile(".global ram_fault_read; ram_fault_read: movb (%0),%%al"
                : : "r"(ram_address) : "eax","memory");
            return 72;
        } else if (command==4) {
            ram_result=liberums_call(SYS_PAGE_MAP,ram_index,ram_generation,RESOURCE_WINDOW,3,0);
            if (ram_result!=RES_ESTALE) { return 73; }
            observed(4);
        } else if (command==5) { /* Raw RELEASE supersedes a pending request. */
            ram_result=liberums_call(SYS_PAGE_RELEASE,ram_index,ram_generation,0,0,0);
            observed(5);
        } else if (command==6) {
            ram_result=liberums_call(SYS_REVOKE_ACK,ram_index,ram_generation,ram_request,0,0);
            observed(6);
        } else if (command==7) {
            if (allocation && ael_app_free(context,allocation)) { return 74; }
            return 0;
        } else if (command==8) { __asm__ volatile("ud2"); }
        else if (command==9) { liberums_exit(0); } /* Deliberate EXIT with live RAM. */
        else if (command==10) { /* A fresh claim after ACK must return zero RAM. */
            uint32_t gen=0; int32_t index=liberums_call(SYS_PAGE_SPECIFIC,ram_pfn,RESOURCE_ALL,0,0,&gen);
            if (index<1 || liberums_call(SYS_PAGE_MAP,(uint32_t)index,gen,RESOURCE_WINDOW,3,0)) { return 75; }
            volatile uint32_t *page=(void *)RESOURCE_WINDOW;
            for (unsigned i=0;i<1024;++i) { if (page[i]) { return 76; } }
            if (liberums_call(SYS_PAGE_RELEASE,(uint32_t)index,gen,0,0,0)) { return 77; }
            observed(7);
        } else if (command==11) {
            ram_result=liberums_call(SYS_PAGE_MAP,ram_index,ram_generation,RESOURCE_WINDOW+4096,3,0);
            if (ram_result) { return 81; }
            observed(8);
        } else { return 78; }
    }
}
