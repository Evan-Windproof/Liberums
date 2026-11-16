/* SPDX-License-Identifier: GPL-3.0-or-later */
/* QEMU i8042 first port only. No mouse, generic input bus, or user port I/O. */
#include <arch/x86/ps2.h>
#include <arch/x86/io.h>
#define WAIT_LIMIT 100000u
#define DRAIN_LIMIT 256u
static bool input_ready(void)
{
    for (uint32_t i = 0; i < WAIT_LIMIT; ++i) {
        if ((inb(0x64) & 2) == 0) { return true; }
    }
    return false;
}
static bool command(uint8_t value)
{
    if (!input_ready()) { return false; }
    outb(0x64, value);
    return true;
}
static bool data_write(uint8_t value)
{
    if (!input_ready()) { return false; }
    outb(0x60, value);
    return true;
}
enum ps2_result ps2_read(uint8_t *byte)
{
    uint8_t status = inb(0x64);
    if ((status & 1) == 0) { return PS2_EMPTY; }
    *byte = inb(0x60);
    /* AUX, timeout or parity failure: consume but never deliver to keyboard. */
    return (status & 0xe0) != 0 ? PS2_DISCARD : PS2_DATA;
}
static bool response(uint8_t *byte)
{
    for (uint32_t i = 0; i < WAIT_LIMIT; ++i) {
        enum ps2_result result = ps2_read(byte);
        if (result == PS2_DATA) { return true; }
        if (result == PS2_DISCARD) { return false; }
    }
    return false;
}
static bool keyboard_command(uint8_t value)
{
    uint8_t ack;
    return data_write(value) && response(&ack) && ack == 0xfa;
}
static bool drain(void)
{
    uint8_t ignored;
    for (uint32_t i = 0; i < DRAIN_LIMIT; ++i) {
        if (ps2_read(&ignored) == PS2_EMPTY) { return true; }
    }
    return (inb(0x64) & 1) == 0;
}
void ps2_stop(void)
{
    (void)command(0xad); /* Disable first port clock; caller already masks IRQ1. */
    (void)drain();
}
bool ps2_start(void)
{
    uint8_t config;
    if (!command(0xad) || !command(0xa7) || !drain()
        || !command(0x20) || !response(&config)) { return false; }
    /* Keep IRQ1 masked at PIC during ACK exchange; never reset the PIC/PIT.
     * Enable translation (set 2 -> set 1), disable AUX/IRQ12, enable port 1. */
    config = (config | 0x61u) & ~0x12u;
    if (!command(0x60) || !data_write(config) || !command(0xae)
        || !keyboard_command(0xf5) || !keyboard_command(0xf0)
        || !keyboard_command(2) || !keyboard_command(0xf4) || !drain()) {
        ps2_stop();
        return false;
    }
    return true;
}
