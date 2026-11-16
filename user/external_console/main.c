/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Ordinary freestanding ELF entry. No fixed data-page address or kernel header. */
#include <abi/elf.h>
#include "../console/core.h"
__attribute__((section(".note.liberums"), used, aligned(4)))
static const struct liberums_abi_note requirement = {
    9, 12, LIBERUMS_NOTE_TYPE, "LIBERUMS", 1, 2,
    LIBERUMS_FEATURE_ELF32 | LIBERUMS_FEATURE_PREEMPTION | LIBERUMS_FEATURE_LIMITS
};
static struct console console_state; /* Normal ELF BSS, zeroed by the loader. */
#ifdef CONSOLE_ALTERNATE
static const char welcome[] = "Independent Amber Console\nSame public VGA and keyboard ABI\nType help for commands\n";
struct { uint8_t foreground, background; } console_style = {14, 0};
#else
static const char welcome[] = "Liberums Exokernel\nExternal ELF Interactive Console\nType help for commands\n";
struct { uint8_t foreground, background; } console_style = {15, 1};
#endif
volatile uint32_t console_observation[3]; /* stage, raw, monotonic user observation */
/* Checkpoints observe execution only; normal input does not depend on them. */
__attribute__((noinline)) void external_console_checkpoint(uint32_t stage, uint32_t raw)
{
    console_observation[0] = stage;
    console_observation[1] = raw;
    ++console_observation[2];
    __asm__ volatile (".global external_console_observed; external_console_observed: nop" : : : "memory");
}
#ifdef CONSOLE_TEST
volatile uint32_t console_test_request; /* Private user state, never a kernel API. */
#endif
__attribute__((section(".text.entry"))) _Noreturn void _start(void)
{
    uint32_t format, features;
    int32_t version = liberums_call(SYS_ABI_QUERY, 0, 0, 0, 0, &features);
    if (!liberums_abi_compatible((uint32_t)version, features, 1, 2, requirement.required_features)
        || liberums_call(SYS_KBD_INFO, 1, 0, 0, 0, &format) != KBD_QUEUE_CAPACITY || format != KBD_SCANSET1) {
        liberums_exit(84);
    }
    console_state.keyboard.index = (uint32_t)liberums_call(SYS_KBD_ACQUIRE, 1, 0, 0, 0, &console_state.keyboard.generation);
    if (console_state.keyboard.index != KBD_HANDLE) { liberums_exit(85); }
    console_state.vga.index = (uint32_t)liberums_call(SYS_VGA_ACQUIRE, 1, 3, 0, 0, &console_state.vga.generation);
    if (console_state.vga.index != VGA_DEVICE_HANDLE) { liberums_exit(86); }
    if (liberums_call(SYS_VGA_MAP, console_state.vga.index, console_state.vga.generation, VGA_USER_ADDRESS, 3, 0) != 0) {
        liberums_exit(87);
    }
    console_start(&console_state, welcome, console_style.foreground, console_style.background);
    external_console_checkpoint(1, 0);
    for (;;) {
#ifdef CONSOLE_TEST
        if (console_test_request == 1) { liberums_exit(0); }
        if (console_test_request == 2) { __asm__ volatile ("ud2"); }
        if (console_test_request == 3) {
            liberums_call(SYS_YIELD, 0, 0, 0, 0, 0);
            int32_t denied = liberums_call(SYS_KBD_POLL, console_state.keyboard.index, console_state.keyboard.generation, 0, 0, 0);
            if (denied != RES_ESTALE || liberums_call(SYS_KBD_ACQUIRE, 1, 0, 0, 0, 0) != RES_EPERM) { liberums_exit(91); }
            console_cursor(&console_state); /* VGA authority survives keyboard revoke. */
            liberums_exit(0);
        }
        if (console_test_request == 4) {
            liberums_call(SYS_YIELD, 0, 0, 0, 0, 0);
            int32_t key = liberums_call(SYS_KBD_STATS, console_state.keyboard.index, console_state.keyboard.generation, 0, 0, 0);
            if (key < 0 || liberums_call(SYS_VGA_MAP, console_state.vga.index, console_state.vga.generation, VGA_USER_ADDRESS, 3, 0) != RES_ESTALE) { liberums_exit(92); }
            *(volatile uint16_t *)VGA_USER_ADDRESS = 0x4f58; /* Must #PF after trusted revoke. */
        }
#endif
        uint32_t raw;
        int32_t result = liberums_call(SYS_KBD_POLL, console_state.keyboard.index,
                                       console_state.keyboard.generation, 0, 0, &raw);
        if (result == RES_EAGAIN) { liberums_call(SYS_YIELD, 0, 0, 0, 0, 0); continue; }
        if (result == RES_EOVERFLOW) { console_reset_input(&console_state); }
        else if (result == 0 && raw <= 255) { console_feed(&console_state, (uint8_t)raw); }
        else { liberums_exit(90); }
        external_console_checkpoint(2, raw);
    }
}
