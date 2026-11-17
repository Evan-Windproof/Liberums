/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "pool.h"
struct object_pool pool;
/* Private workload inputs for the tutorial/GDB experiments, not authority. */
volatile uint32_t lesson_command,lesson_gate,lesson_progress;
volatile int32_t lesson_result;
static void wait_for_gate(void)
{
    while (!lesson_gate) { libos_yield(); }
}
int libos_main(void)
{
    if (pool_open(&pool)) { return 60; }
    unsigned char *objects[OBJECT_COUNT];
    for (uint32_t i=0;i<OBJECT_COUNT;++i) {
        objects[i]=pool_take(&pool);
        if (!objects[i]) { return 61; }
        for (uint32_t j=0;j<OBJECT_BYTES;++j) { objects[i][j]=(uint8_t)(i^j^0x5a); }
    }
    if (pool_take(&pool)!=0 || pool_give(&pool,objects[0]+1)!=RES_EINVAL) { return 62; }
    example_observe(1,pool.address);
    if ((lesson_command==1 || lesson_command==2 || lesson_command==3)
        && !(libos_info.features&LIBERUMS_FEATURE_REVOCATION)) { return 93; }
    if (lesson_command==5) { wait_for_gate(); }
    if (lesson_command==2 || lesson_command==3) {
        wait_for_gate();
        if (lesson_command==3) {
            __asm__ volatile(".global lesson_fault_read; lesson_fault_read: movl (%0),%%eax" : : "r"(pool.address) : "eax","memory");
            return 63;
        }
        lesson_result=pool_check(&pool);
        example_observe(4,(uint32_t)lesson_result);
        return lesson_result==RES_ESTALE ? 96 : 64;
    }
    if (lesson_command==4) { __asm__ volatile("ud2"); }
    __asm__ volatile(".global lesson_cpu_begin; lesson_cpu_begin: nop" : : : "memory");
    for (lesson_progress=0;lesson_progress<12000000u;++lesson_progress) { __asm__ volatile("" : : : "memory"); }
    __asm__ volatile(".global lesson_cpu_end; lesson_cpu_end: nop" : : : "memory");
    for (uint32_t i=0;i<OBJECT_COUNT;++i) {
        for (uint32_t j=0;j<OBJECT_BYTES;++j) { if (objects[i][j]!=(uint8_t)(i^j^0x5a)) { return 65; } }
        if (pool_give(&pool,objects[i])) { return 66; }
    }
    if (pool_give(&pool,objects[0])!=RES_EINVAL) { return 67; }
    example_observe(2,pool.address);
    if (lesson_command==1) {
        wait_for_gate();lesson_result=pool_ack_idle(&pool);
        if (lesson_result) { return 68; }
    } else if (pool_close(&pool)) { return 69; }
    if (liberums_call(SYS_PAGE_QUERY,pool.handle.index,pool.handle.generation,0,0,0)!=RES_ESTALE) { return 70; }
    example_observe(3,pool.handle.generation);
    return libos_log("Bitmap object pool passed\n") ? 71 : 0;
}
