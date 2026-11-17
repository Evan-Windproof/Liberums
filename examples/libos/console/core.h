/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_CONSOLE_CORE_H
#define LIBERUMS_CONSOLE_CORE_H
#include <abi/keyboard.h>
#include <abi/vga.h>
#include <stdbool.h>
#define LINE_LIMIT 63u
struct console {
    struct resource_handle keyboard, vga;
    uint32_t row, column, length, commands, running;
    uint8_t shift, caps, caps_down, extended, pause;
    char line[LINE_LIMIT + 1];
    uint8_t foreground, background;
};

void console_start(struct console *state, const char *welcome, uint8_t foreground, uint8_t background);
void console_feed(struct console *state, uint8_t raw);
void console_cursor(struct console *state);
void console_reset_input(struct console *state);
#endif
