/* SPDX-License-Identifier: GPL-3.0-or-later */
/* User policy only: colors, clear, newline and layout. No kernel text renderer. */
#include <abi/vga.h>
#define DATA ((volatile uint32_t *)0x08001000u)
#define SCREEN ((volatile uint16_t *)VGA_USER_ADDRESS)
static void expect(int condition, uint32_t code)
{
    if (!condition) { liberums_exit(code); }
}
static int32_t operation(uint32_t call, struct resource_handle h, uint32_t va, uint32_t rights)
{
    return liberums_call(call, h.index, h.generation, va, rights, 0);
}
static struct resource_handle acquire(uint32_t rights)
{
    uint32_t generation = 0;
    int32_t index = liberums_call(SYS_VGA_ACQUIRE, VGA_DEVICE_TEXT, rights, 0, 0, &generation);
    expect(index == (int32_t)VGA_DEVICE_HANDLE && generation != 0, 10);
    return (struct resource_handle){(uint32_t)index, generation};
}
__attribute__((noinline)) static void checkpoint(uint32_t stage)
{
    DATA[2] = stage;
    __asm__ volatile (".global vga_user_checkpoint; vga_user_checkpoint:" : : : "memory");
    liberums_call(SYS_YIELD, 0, 0, 0, 0, 0);
}
static void clear(uint8_t color)
{
    for (uint32_t i = 0; i < VGA_COLUMNS * VGA_ROWS; ++i) {
        SCREEN[i] = (uint16_t)color << 8 | ' ';
    }
}
static void text(const char *s, uint32_t *cursor, uint8_t color)
{
    while (*s != '\0') {
        if (*s == '\n') {
            *cursor = (*cursor / VGA_COLUMNS + 1) * VGA_COLUMNS;
        } else if (*cursor < VGA_COLUMNS * VGA_ROWS) {
            SCREEN[(*cursor)++] = (uint16_t)color << 8 | (uint8_t)*s;
        }
        ++s;
    }
}
__attribute__((section(".text.entry"))) _Noreturn void user_entry(void)
{
    uint32_t bytes;
    expect(liberums_call(SYS_VGA_INFO, VGA_DEVICE_TEXT, 0, 0, 0, &bytes)
        == (int32_t)(VGA_COLUMNS | VGA_ROWS << 16) && bytes == VGA_MAPPING_BYTES, 11);
    expect(liberums_call(SYS_VGA_INFO, 2, 0, 0, 0, &bytes) == RES_ENOENT && bytes == 0, 12);
    expect(liberums_call(SYS_PAGE_SPECIFIC, 0xb8, RESOURCE_ALL, 0, 0, 0) == RES_EPERM, 13);
    if (DATA[0] == 2) {
        expect(liberums_call(SYS_VGA_ACQUIRE, 1, 3, 0, 0, 0) == RES_EPERM, 14);
        struct resource_handle stolen = {DATA[3], DATA[4]};
        expect(operation(SYS_VGA_MAP, stolen, VGA_USER_ADDRESS, RESOURCE_READ) == RES_EPERM, 15);
        expect(operation(SYS_VGA_RELEASE, stolen, 0, 0) == RES_EPERM, 16);
        DATA[2] = 9;
        /* The peer guesses the exact VA AND live handle: MMU still denies it. */
        *(volatile uint16_t *)VGA_USER_ADDRESS = 0x4f58;
        liberums_exit(99);
    }
    struct resource_handle h = acquire(RESOURCE_READ);
    DATA[3] = h.index; DATA[4] = h.generation;
    expect(operation(SYS_VGA_MAP, h, VGA_USER_ADDRESS, 3) == RES_EPERM, 17);
    expect(operation(SYS_VGA_MAP, h, 0x100000, 1) == RES_EINVAL, 18);
    expect(operation(SYS_VGA_MAP, h, VGA_USER_ADDRESS + 4096, 1) == RES_EINVAL, 19);
    expect(operation(SYS_VGA_MAP, h, VGA_USER_ADDRESS + 1, 1) == RES_EINVAL, 20);
    expect(operation(SYS_VGA_MAP, h, VGA_USER_ADDRESS, 0) == RES_EINVAL, 21);
    expect(operation(SYS_PAGE_MAP, h, 0x09000000, 1) == RES_ESTALE, 22);
    expect(operation(SYS_PAGE_RELEASE, h, 0, 0) == RES_ESTALE, 23);
    expect(operation(SYS_PAGE_TRANSFER, h, 2, 1) == RES_ESTALE, 24);
    expect(liberums_call(SYS_VGA_ACQUIRE, 1, 1, 0, 0, 0) == RES_EBUSY, 25);
    expect(operation(SYS_VGA_MAP, h, VGA_USER_ADDRESS, 1) == 0, 26);
    expect(operation(SYS_VGA_MAP, h, VGA_USER_ADDRESS, 1) == RES_EBUSY, 27);
    DATA[6] = SCREEN[0]; /* Actual read through the read-only device binding. */
    checkpoint(1); /* Read-only mapping, exact live-handle peer attack. */
    if (DATA[1] == 4) {
        SCREEN[0] = 0x4f58;
        liberums_exit(98);
    }
    expect(operation(SYS_VGA_RELEASE, h, 0, 0) == 0, 28);
    expect(operation(SYS_VGA_MAP, h, VGA_USER_ADDRESS, 1) == RES_ESTALE, 29);
    expect(operation(SYS_VGA_RELEASE, h, 0, 0) == RES_ESTALE, 30);
    struct resource_handle old = h;
    h = acquire(3);
    expect(h.generation != old.generation, 31);
    expect(operation(SYS_VGA_MAP, old, VGA_USER_ADDRESS, 1) == RES_ESTALE, 32);
    expect(operation(SYS_VGA_MAP, h, VGA_USER_ADDRESS, 3) == 0, 33);
    DATA[3] = h.index; DATA[4] = h.generation;
    clear(0x17);
    uint32_t cursor = 0;
    text("Liberums Exokernel\n", &cursor, 0x1f);
    text("VGA Text Console Prototype\n", &cursor, 0x1e);
    text("Ring 3: running\nVGA: authorized device mapping\nCOM1: retained\nABI 1.2: compatible", &cursor, 0x1a);
    checkpoint(2);
    switch (DATA[1]) {
    case 1: /* Trusted harness has revoked the device before this resumes. */
        expect(operation(SYS_VGA_MAP, h, VGA_USER_ADDRESS, 3) == RES_ESTALE, 34);
        expect(liberums_call(SYS_VGA_ACQUIRE, 1, 3, 0, 0, 0) == RES_EPERM, 35);
        SCREEN[0] = 0x4f58;
        break;
    case 2: *(volatile uint16_t *)(VGA_USER_ADDRESS + 4096) = 0x4f58; break;
    case 3: *(volatile uint16_t *)0xb8000 = 0x4f58; break;
    case 5: *(volatile uint32_t *)0x100000 = 0; break;
    case 6: __asm__ volatile ("outb %%al, %%dx" : : "a"(0), "d"(0x3d4)); break;
    case 7: *(volatile uint32_t *)(uintptr_t)DATA[5] = 0; break;
    case 8:
        expect(operation(SYS_VGA_RELEASE, h, 0, 0) == 0, 36);
        expect(operation(SYS_VGA_MAP, h, VGA_USER_ADDRESS, 3) == RES_ESTALE, 37);
        SCREEN[0] = 0x4f58;
        break;
    case 10: *(volatile uint8_t *)(VGA_USER_ADDRESS - 1) = 0; break;
    default: liberums_exit(0); /* Deliberately leave binding for exit cleanup. */
    }
    liberums_exit(97); /* Every hostile access above must trap instead. */
}
