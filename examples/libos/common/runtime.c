/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "runtime.h"
#ifndef LIBOS_MIN_MINOR
#define LIBOS_MIN_MINOR LIBERUMS_ABI_MINOR
#endif
#ifdef LIBOS_COOPERATIVE
#define PROFILE_FEATURE LIBERUMS_FEATURE_COOPERATIVE
#else
#define PROFILE_FEATURE LIBERUMS_FEATURE_PREEMPTION
#endif
#ifndef LIBOS_PAGE_FEATURES
#define LIBOS_PAGE_FEATURES 0
#endif
static const struct liberums_abi_note requirement
    __attribute__((section(".note.liberums"),used,aligned(4)))={
        9,12,LIBERUMS_NOTE_TYPE,"LIBERUMS",LIBERUMS_ABI_MAJOR,LIBOS_MIN_MINOR,
        LIBERUMS_FEATURE_ELF32|LIBERUMS_FEATURE_LIMITS|PROFILE_FEATURE|LIBOS_PAGE_FEATURES
    };
struct libos_info libos_info; /* Ordinary ELF BSS, never revocable payload. */
volatile uint32_t example_stage,example_value,example_sequence;
__attribute__((noinline)) void example_observe(uint32_t stage,uint32_t value)
{
    example_stage=stage;example_value=value;++example_sequence;
    __asm__ volatile(".global example_observed; example_observed: nop" : : : "memory");
}
int libos_log(const char *message)
{
    while (*message) {
        int32_t error=liberums_call(SYS_LOG_CHAR,(uint8_t)*message++,0,0,0,0);
        if (error) { return error; } /* The kernel's 128-character quota is real. */
    }
    return 0;
}
void libos_yield(void) { (void)liberums_call(SYS_YIELD,0,0,0,0,0); }
_Noreturn void libos_exit(uint32_t status) { liberums_exit(status); }
_Noreturn void libos_start(void)
{
    int32_t version=liberums_call(SYS_ABI_QUERY,0,0,0,0,&libos_info.features);
    if (version<0 || !liberums_abi_compatible((uint32_t)version,libos_info.features,
            LIBERUMS_ABI_MAJOR,LIBOS_MIN_MINOR,requirement.required_features)) { libos_exit(90); }
    libos_info.version=(uint32_t)version;
    int32_t quota=liberums_call(SYS_RESOURCE_LIMITS,0,0,0,0,&libos_info.bindings);
    if (quota<1 || libos_info.bindings<1) { libos_exit(91); }
    libos_info.quota=(uint32_t)quota;
    libos_exit((uint32_t)libos_main());
}
