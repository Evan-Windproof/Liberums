/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Legacy single-page test launcher. All console policy is shared user code. */
#include "console/core.h"
#define DATA ((volatile uint32_t *)0x08001000u)
#define C ((struct console *)0x08001100u)
static void expect(int condition, uint32_t code)
{
    if (!condition) { liberums_exit(code); }
}
static int32_t call(uint32_t number, struct resource_handle handle, uint32_t value, uint32_t *extra)
{
    return liberums_call(number, handle.index, handle.generation, value, 0, extra);
}
__attribute__((noinline)) static void checkpoint(uint32_t stage)
{
    DATA[2] = stage;
    __asm__ volatile (".global interactive_checkpoint; interactive_checkpoint:" : : : "memory");
}
__attribute__((section(".text.entry"))) _Noreturn void user_entry(void)
{
    if (DATA[0] == 2) {
        expect(liberums_call(SYS_KBD_ACQUIRE, 1, 0, 0, 0, 0) == RES_EPERM, 80);
        while (DATA[3] == 0) { liberums_call(SYS_YIELD, 0, 0, 0, 0, 0); }
        struct resource_handle stolen = {DATA[3], DATA[4]};
        expect(call(SYS_KBD_POLL, stolen, 0, 0) == RES_EPERM, 81);
        expect(call(SYS_KBD_RELEASE, stolen, 0, 0) == RES_EPERM, 82);
        expect(call(SYS_VGA_CURSOR, (struct resource_handle){VGA_DEVICE_HANDLE, DATA[5]}, 0, 0) == RES_EPERM, 83);
        DATA[2] = 10;
        /* Deliberately non-cooperative peer: IRQ0 must keep the Console live. */
        __asm__ volatile (
            "mov $0x11111111, %%eax; mov $0x22222222, %%ebx; mov $0x33333333, %%ecx;"
            "mov $0x44444444, %%edx; mov $0x55555555, %%esi; mov $0x66666666, %%edi;"
            "mov $0x77777777, %%ebp; std; stc;"
            ".global interactive_peer_spin; interactive_peer_spin: jmp interactive_peer_spin" : : : "memory");
        __builtin_unreachable();
    }
    uint32_t features;
    expect((uint32_t)liberums_call(SYS_ABI_QUERY, 0, 0, 0, 0, &features) == LIBERUMS_ABI_VERSION
        && (features & LIBERUMS_FEATURE_PREEMPTION) != 0
        && (features & LIBERUMS_FEATURE_COOPERATIVE) == 0, 96);
    uint32_t format;
    expect(liberums_call(SYS_KBD_INFO, 1, 0, 0, 0, &format) == KBD_QUEUE_CAPACITY && format == KBD_SCANSET1, 84);
    C->keyboard.index = (uint32_t)liberums_call(SYS_KBD_ACQUIRE, 1, 0, 0, 0, &C->keyboard.generation);
    expect(C->keyboard.index == KBD_HANDLE, 85);
    C->vga.index = (uint32_t)liberums_call(SYS_VGA_ACQUIRE, 1, 3, 0, 0, &C->vga.generation);
    expect(C->vga.index == VGA_DEVICE_HANDLE, 86);
    expect(call(SYS_VGA_CURSOR, C->vga, 0, 0) == RES_ENOENT, 97);
    expect(liberums_call(SYS_VGA_MAP, C->vga.index, C->vga.generation, VGA_USER_ADDRESS, 3, 0) == 0, 87);
    expect(call(SYS_VGA_CURSOR, C->vga, 2000, 0) == RES_EINVAL, 98);
    DATA[3] = C->keyboard.index; DATA[4] = C->keyboard.generation; DATA[5] = C->vga.generation;
    console_start(C, "Liberums Exokernel\nKeyboard Interactive Console\nType help for commands\n", 15, 1);
    checkpoint(1);
    /* Explicit overflow fixture only: allow real IRQ1 to fill the queue while
     * this CPL3 consumer waits. Tests release the wait via its own private data. */
    if (DATA[1] == 2) { while (DATA[8] == 0) { __asm__ volatile ("pause" : : : "memory"); } }
    for (;;) {
        if (DATA[9] != 0) {
            switch (DATA[9]) {
            case 2: __asm__ volatile ("inb %%dx, %%al" : : "d"(0x60) : "eax"); break;
            case 3: __asm__ volatile ("outb %%al, %%dx" : : "a"(0), "d"(0x64)); break;
            case 4: __asm__ volatile ("int $0x21"); break;
            case 5: __asm__ volatile ("ud2"); break;
            case 7: {
                struct resource_handle old = C->keyboard;
                expect(call(SYS_KBD_RELEASE, old, 0, 0) == 0, 91);
                expect(call(SYS_KBD_POLL, old, 0, 0) == RES_ESTALE, 92);
                C->keyboard.index = (uint32_t)liberums_call(SYS_KBD_ACQUIRE, 1, 0, 0, 0, &C->keyboard.generation);
                expect(C->keyboard.index == KBD_HANDLE && C->keyboard.generation != old.generation, 93);
                expect(call(SYS_KBD_POLL, old, 0, 0) == RES_ESTALE, 94);
                expect(call(SYS_KBD_POLL, C->keyboard, 0, 0) == RES_EAGAIN, 95);
                break;
            }
            case 6:
                liberums_call(SYS_YIELD, 0, 0, 0, 0, 0);
                expect(call(SYS_KBD_POLL, C->keyboard, 0, 0) == RES_ESTALE, 88);
                expect(liberums_call(SYS_KBD_ACQUIRE, 1, 0, 0, 0, 0) == RES_EPERM, 89);
                break;
            default: break;
            }
            liberums_exit(0);
        }
        uint32_t raw;
        int32_t result = call(SYS_KBD_POLL, C->keyboard, 0, &raw);
        if (result == RES_EAGAIN) { liberums_call(SYS_YIELD, 0, 0, 0, 0, 0); continue; }
        if (result == RES_EOVERFLOW) {
            console_reset_input(C);
            DATA[7] = raw; checkpoint(3); continue;
        }
        expect(result == 0 && raw <= 255, 90);
        ++DATA[6]; DATA[7] = raw;
        console_feed(C, (uint8_t)raw); checkpoint(2);
    }
}
