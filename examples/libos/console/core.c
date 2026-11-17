/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Teaching adaptation of Liberums user/console/core.c at Phase 9.
 * This exported copy is user policy and has no kernel/private dependency. */
#include "core.h"
#define SCREEN ((volatile uint16_t *)VGA_USER_ADDRESS)
static void expect(int condition, uint32_t code)
{
    if (!condition) { liberums_exit(code); }
}
static int32_t call(uint32_t number, struct resource_handle handle, uint32_t value, uint32_t *extra)
{
    return liberums_call(number, handle.index, handle.generation, value, 0, extra);
}
void console_cursor(struct console *C)
{
    expect(call(SYS_VGA_CURSOR, C->vga, C->row * VGA_COLUMNS + C->column, 0) == 0, 70);
}
static void newline(struct console *C)
{
    C->column = 0;
    if (++C->row == VGA_ROWS) {
        for (uint32_t i = 0; i < VGA_COLUMNS * (VGA_ROWS - 1); ++i) { SCREEN[i] = SCREEN[i + VGA_COLUMNS]; }
        for (uint32_t i = VGA_COLUMNS * (VGA_ROWS - 1); i < VGA_COLUMNS * VGA_ROWS; ++i) { SCREEN[i] = ((uint16_t)(C->background << 4 | 7) << 8 | 32); }
        --C->row;
    }
}
static void put(struct console *C, char ch)
{
    if (ch == '\n') { newline(C); return; }
    SCREEN[C->row * VGA_COLUMNS + C->column] = ((uint16_t)(C->background << 4 | C->foreground) << 8) | (uint8_t)ch;
    if (++C->column == VGA_COLUMNS) { newline(C); }
}
static void text(struct console *C, const char *s)
{
    while (*s != '\0') { put(C, *s++); }
}
static void clear(struct console *C)
{
    for (uint32_t i = 0; i < VGA_COLUMNS * VGA_ROWS; ++i) { SCREEN[i] = ((uint16_t)(C->background << 4 | 7) << 8 | 32); }
    C->row = C->column = 0;
}
static void number(struct console *C, uint32_t n)
{
    char digits[10]; uint32_t length = 0;
    do { digits[length++] = '0' + n % 10; n /= 10; } while (n != 0);
    while (length != 0) { put(C, digits[--length]); }
}
static int equal(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void prompt(struct console *C) { C->length = 0; C->line[0] = '\0'; text(C, "> "); }
static void command(struct console *C)
{
    C->line[C->length] = '\0';
    newline(C);
    if (C->length != 0) {
        ++C->commands;
        if (equal(C->line, "help")) { text(C, "help clear about status exit\n"); }
        else if (equal(C->line, "exit")) { text(C, "Console finished\n"); C->running = 0; }
        else if (equal(C->line, "clear")) { clear(C); }
        else if (equal(C->line, "about")) { text(C, "Independent teaching console - user policy\n"); }
        else if (equal(C->line, "status")) {
            uint32_t lost = 0;
            int32_t queued = call(SYS_KBD_STATS, C->keyboard, 0, &lost);
            expect(queued >= 0, 71);
            text(C, "Ring 3 | ABI 1.2 | IRQ0 preempt | PS/2 IRQ1\ncommands=");
            number(C, C->commands); text(C, " queue="); number(C, (uint32_t)queued);
            text(C, " lost="); number(C, lost); newline(C);
        } else { text(C, "Unknown command; type help\n"); }
    }
    prompt(C);
}
/* US set-1 table is user policy. Breaks, E0/E1 and modifier state never enter
 * kernel policy. Ctrl/Alt/navigation have no command meaning in this prototype. */
static const char unshifted[128] = {
    [2]='1',[3]='2',[4]='3',[5]='4',[6]='5',[7]='6',[8]='7',[9]='8',[10]='9',[11]='0',
    [12]='-',[13]='=',[16]='q',[17]='w',[18]='e',[19]='r',[20]='t',[21]='y',[22]='u',[23]='i',[24]='o',[25]='p',
    [26]='[',[27]=']',[30]='a',[31]='s',[32]='d',[33]='f',[34]='g',[35]='h',[36]='j',[37]='k',[38]='l',
    [39]=';',[40]='\'',[41]='`',[43]='\\',[44]='z',[45]='x',[46]='c',[47]='v',[48]='b',[49]='n',[50]='m',
    [51]=',',[52]='.',[53]='/',[57]=' '
};
static const char shifted_digits[] = "!@#$%^&*()_+";
static char translate(struct console *C, uint8_t raw)
{
    if (C->pause != 0) { --C->pause; return 0; }
    if (raw == 0xe1) { C->pause = 5; return 0; }
    if (raw == 0xe0) { C->extended = 1; return 0; }
    if (C->extended) { C->extended = 0; return 0; }
    uint8_t code = raw & 0x7f;
    bool release = (raw & 0x80) != 0;
    if (code == 42 || code == 54) {
        uint8_t mask = code == 42 ? 1 : 2;
        if (release) { C->shift &= ~mask; } else { C->shift |= mask; }
        return 0;
    }
    if (code == 58) {
        if (!release && !C->caps_down) { C->caps ^= 1; }
        C->caps_down = !release;
        return 0;
    }
    if (release) { return 0; }
    if (code == 14) { return '\b'; }
    if (code == 28) { return '\n'; }
    char ch = unshifted[code];
    if (ch >= 'a' && ch <= 'z') {
        if ((C->shift != 0) != (C->caps != 0)) { ch -= 'a' - 'A'; }
    } else if (C->shift) {
        if (code >= 2 && code <= 13) { ch = shifted_digits[code - 2]; }
        else {
            const char *plain = "[];'`\\,./";
            const char *shift = "{}:\"~|<>?";
            for (uint32_t i = 0; plain[i] != '\0'; ++i) { if (ch == plain[i]) { ch = shift[i]; break; } }
        }
    }
    return ch;
}
static void edit(struct console *C, char ch)
{
    if (ch == '\n') { command(C); }
    else if (ch == '\b') {
        if (C->length != 0) {
            --C->length; --C->column;
            C->line[C->length] = '\0';
            SCREEN[C->row * VGA_COLUMNS + C->column] = ((uint16_t)(C->background << 4 | 7) << 8 | 32);
        }
    } else if (ch >= 32 && ch <= 126 && C->length < LINE_LIMIT) {
        C->line[C->length++] = ch;
        C->line[C->length] = '\0';
        put(C, ch);
    }
}

void console_start(struct console *C, const char *welcome, uint8_t foreground, uint8_t background)
{
    C->running = 1;
    C->foreground = foreground & 15;
    C->background = background & 7;
    clear(C);
    text(C, welcome);
    prompt(C);
    console_cursor(C);
}
void console_feed(struct console *C, uint8_t raw)
{
    edit(C, translate(C, raw));
    console_cursor(C);
}
void console_reset_input(struct console *C)
{
    C->shift = C->caps = C->caps_down = C->extended = C->pause = 0;
    newline(C);
    text(C, "Keyboard overflow: input reset\n");
    prompt(C);
    console_cursor(C);
}
