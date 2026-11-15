/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <arch/x86/gdt.h>
#include <stddef.h>

/* 32-bit architectural TSS layout; unused hardware-task fields stay zero. */
struct task_state {
    uint32_t previous, esp0, ss0, esp1, ss1, esp2, ss2;
    uint32_t cr3, eip, eflags, eax, ecx, edx, ebx, esp, ebp, esi, edi;
    uint32_t es, cs, ss, ds, fs, gs, ldt;
    uint16_t trap, io_map_base;
} __attribute__((packed));
static struct task_state tss;
static uint64_t runtime_gdt[6] __attribute__((aligned(8)));
_Static_assert(sizeof(struct task_state) == 104, "32-bit TSS size");
_Static_assert(offsetof(struct task_state, io_map_base) == 102, "I/O bitmap offset");

void gdt_init(void)
{
    runtime_gdt[0] = 0;
    /* Accessed bits are set in advance; no descriptor writes are needed later. */
    runtime_gdt[1] = UINT64_C(0x00cf9b000000ffff);
    runtime_gdt[2] = UINT64_C(0x00cf93000000ffff);
    runtime_gdt[3] = UINT64_C(0x00cffb000000ffff);
    runtime_gdt[4] = UINT64_C(0x00cff3000000ffff);
    uint32_t base = (uintptr_t)&tss;
    uint32_t limit = sizeof(tss) - 1;
    runtime_gdt[5] = (uint64_t)limit | ((uint64_t)(base & 0xffffff) << 16)
        | (UINT64_C(0x89) << 40) | ((uint64_t)(base >> 24) << 56);
    tss.ss0 = 0x10;
    /* Bitmap begins beyond the descriptor limit: all user port I/O is denied. */
    tss.io_map_base = sizeof(tss);
    const struct {
        uint16_t limit;
        uint32_t base;
    } __attribute__((packed)) pointer = {sizeof(runtime_gdt) - 1, (uintptr_t)runtime_gdt};
    __asm__ volatile ("lgdt %0" : : "m"(pointer) : "memory");
    /* Existing CS/DS caches describe the same flat ring-0 segments. */
    uint16_t selector = 0x28;
    __asm__ volatile ("ltr %0" : : "rm"(selector) : "memory");
}

void tss_set_kernel_stack(uint32_t top)
{
    tss.esp0 = top;
}
