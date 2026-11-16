/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Boot diagnostic only. The production text renderer lives in user/vga_console.c. */
#include <kernel/vga.h>
#include <kernel/panic.h>
#include <kernel/serial.h>
#include <arch/x86/io.h>

static bool probed, available;
static struct {
    uint32_t mode, columns, rows, page_offset, graphics_misc, crtc_start, pat_supported, pat3;
} vga_boot;

static uint8_t firmware_byte(uint32_t address)
{
    uint8_t value;
    /* BDA is physical low memory, not a C object at a non-null host pointer. */
    __asm__ volatile ("movb (%1), %0" : "=q"(value) : "r"(address) : "memory");
    return value;
}

bool vga_probe(void)
{
    if (probed) {
        return available;
    }
    probed = true;
    /* QEMU pc/SeaBIOS legacy text handoff, not a generic mode-setting driver.
     * Firmware data is advisory: also check actual VGA memory-map/text registers. */
    vga_boot.mode = firmware_byte(0x400 + 0x49);
    vga_boot.columns = firmware_byte(0x400 + 0x4a) | (uint32_t)firmware_byte(0x400 + 0x4b) << 8;
    vga_boot.page_offset = firmware_byte(0x400 + 0x4e) | (uint32_t)firmware_byte(0x400 + 0x4f) << 8;
    vga_boot.rows = firmware_byte(0x400 + 0x84) + 1;
    uint8_t saved = inb(0x3ce);
    outb(0x3ce, 6);
    vga_boot.graphics_misc = inb(0x3cf);
    outb(0x3ce, saved);
    saved = inb(0x3d4);
    outb(0x3d4, 12);
    vga_boot.crtc_start = (uint32_t)inb(0x3d5) << 8;
    outb(0x3d4, 13);
    vga_boot.crtc_start |= inb(0x3d5);
    outb(0x3d4, saved);
    uint32_t ax = 1, bx, cx, dx;
    __asm__ volatile ("cpuid" : "+a"(ax), "=b"(bx), "=c"(cx), "=d"(dx));
    vga_boot.pat_supported = (dx >> 16) & 1;
    if (vga_boot.pat_supported) {
        uint32_t low, high;
        __asm__ volatile ("rdmsr" : "=a"(low), "=d"(high) : "c"(0x277));
        vga_boot.pat3 = low >> 24;
    }
    /* PCD=PWT=1, PAT=0 selects PAT[3]. Require UC rather than assuming
     * firmware left the architectural default. Without PAT use legacy UC. */
    available = (!vga_boot.pat_supported || vga_boot.pat3 == 0)
        && vga_boot.mode == 3 && vga_boot.columns == 80 && vga_boot.rows == 25
        && vga_boot.page_offset == 0 && vga_boot.crtc_start == 0
        && (vga_boot.graphics_misc & 0x0du) == 0x0cu;
    return available;
}

bool vga_available(void)
{
    return available;
}

__attribute__((noinline)) void vga_diagnostic_written(void)
{
    __asm__ volatile ("" : : : "memory");
}

void vga_diagnostic(void)
{
    if (!vga_probe()) {
        panic("VGA requires QEMU legacy mode 3, 80x25, page zero");
    }
    volatile uint16_t *cells = (void *)VGA_PHYSICAL;
    const char text[] = "Liberums Ring 0 VGA diagnostic";
    for (uint32_t i = 0; i < 2000; ++i) {
        cells[i] = 0x1720;
    }
    for (uint32_t i = 0; text[i] != '\0'; ++i) {
        cells[i] = 0x1f00 | (uint8_t)text[i];
    }
    vga_diagnostic_written();
    serial_write("VGA: Ring 0 diagnostic written\n");
}

/* Only a validated device capability may reach this port operation. The user
 * chooses the cell; the kernel enforces the fixed text-mode register boundary. */
bool vga_cursor_set(uint32_t cell)
{
    if (!available || cell >= 2000) { return false; }
    uint8_t saved = inb(0x3d4);
    outb(0x3d4, 15); outb(0x3d5, cell & 255);
    outb(0x3d4, 14); outb(0x3d5, cell >> 8);
    /* Preserve firmware cursor shape, enable it, and verify actual position. */
    outb(0x3d4, 10); uint8_t shape = inb(0x3d5);
    outb(0x3d5, shape & ~0x20u);
    outb(0x3d4, 14); uint32_t actual = (uint32_t)inb(0x3d5) << 8;
    outb(0x3d4, 15); actual |= inb(0x3d5);
    outb(0x3d4, saved);
    return actual == cell;
}
