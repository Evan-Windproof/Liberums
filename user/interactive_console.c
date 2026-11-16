/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Ring3 owns the keymap, editing, commands, colors, scrolling and cursor choice. */
#include <abi/keyboard.h>
#include <stdbool.h>
#include <abi/vga.h>
#define DATA ((volatile uint32_t *)0x08001000u)
#define SCREEN ((volatile uint16_t *)VGA_USER_ADDRESS)
#define LINE_LIMIT 63u
struct console {
    struct resource_handle keyboard, vga;
    uint32_t row, column, length, commands;
    uint8_t shift, caps, caps_down, extended, pause;
    char line[LINE_LIMIT + 1];
};
#define C ((struct console *)0x08001100u)
static void expect(int condition, uint32_t code)
{
    if (!condition) { liberums_exit(code); }
}
static int32_t call(uint32_t number, struct resource_handle handle, uint32_t value, uint32_t *extra)
{
    return liberums_call(number, handle.index, handle.generation, value, 0, extra);
}
static void cursor(void)
{
    expect(call(SYS_VGA_CURSOR, C->vga, C->row * VGA_COLUMNS + C->column, 0) == 0, 70);
}
static void newline(void)
{
    C->column = 0;
    if (++C->row == VGA_ROWS) {
        for (uint32_t i = 0; i < VGA_COLUMNS * (VGA_ROWS - 1); ++i) { SCREEN[i] = SCREEN[i + VGA_COLUMNS]; }
        for (uint32_t i = VGA_COLUMNS * (VGA_ROWS - 1); i < VGA_COLUMNS * VGA_ROWS; ++i) { SCREEN[i] = 0x1720; }
        --C->row;
    }
}
static void put(char ch)
{
    if (ch == '\n') { newline(); return; }
    SCREEN[C->row * VGA_COLUMNS + C->column] = 0x1f00 | (uint8_t)ch;
    if (++C->column == VGA_COLUMNS) { newline(); }
}
static void text(const char *s)
{
    while (*s != '\0') { put(*s++); }
}
static void clear(void)
{
    for (uint32_t i = 0; i < VGA_COLUMNS * VGA_ROWS; ++i) { SCREEN[i] = 0x1720; }
    C->row = C->column = 0;
}
static void number(uint32_t n)
{
    char digits[10]; uint32_t length = 0;
    do { digits[length++] = '0' + n % 10; n /= 10; } while (n != 0);
    while (length != 0) { put(digits[--length]); }
}
static int equal(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) { ++a; ++b; }
    return *a == *b;
}
static void prompt(void) { C->length = 0; C->line[0] = '\0'; text("> "); }
static void command(void)
{
    C->line[C->length] = '\0';
    newline();
    if (C->length != 0) {
        ++C->commands;
        if (equal(C->line, "help")) { text("help clear about status\n"); }
        else if (equal(C->line, "clear")) { clear(); }
        else if (equal(C->line, "about")) { text("Liberums Exokernel - Ring 3 interactive console\n"); }
        else if (equal(C->line, "status")) {
            uint32_t lost = 0;
            int32_t queued = call(SYS_KBD_STATS, C->keyboard, 0, &lost);
            expect(queued >= 0, 71);
            text("Ring 3 | ABI 1.2 | IRQ0 preempt | PS/2 IRQ1\ncommands=");
            number(C->commands); text(" queue="); number((uint32_t)queued);
            text(" lost="); number(lost); newline();
        } else { text("Unknown command; type help\n"); }
    }
    prompt();
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
static char translate(uint8_t raw)
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
static void edit(char ch)
{
    if (ch == '\n') { command(); }
    else if (ch == '\b') {
        if (C->length != 0) {
            --C->length; --C->column;
            C->line[C->length] = '\0';
            SCREEN[C->row * VGA_COLUMNS + C->column] = 0x1720;
        }
    } else if (ch >= 32 && ch <= 126 && C->length < LINE_LIMIT) {
        C->line[C->length++] = ch;
        C->line[C->length] = '\0';
        put(ch);
    }
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
    clear(); text("Liberums Exokernel\nKeyboard Interactive Console\nType help for commands\n"); prompt(); cursor();
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
            C->shift = C->caps = C->caps_down = C->extended = C->pause = 0;
            newline(); text("Keyboard overflow: input reset\n"); prompt(); cursor();
            DATA[7] = raw; checkpoint(3); continue;
        }
        expect(result == 0 && raw <= 255, 90);
        ++DATA[6]; DATA[7] = raw;
        edit(translate((uint8_t)raw)); cursor(); checkpoint(2);
    }
}
