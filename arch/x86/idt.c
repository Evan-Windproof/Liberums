/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <arch/x86/idt.h>
#include <arch/x86/irq.h>
#include <kernel/panic.h>
#include <kernel/domain.h>
#include <kernel/syscall.h>
#include <kernel/serial.h>
#include <stddef.h>

struct idt_gate {
    uint16_t offset_low, selector;
    uint8_t zero, attributes;
    uint16_t offset_high;
} __attribute__((packed));

struct idt_pointer {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

static struct idt_gate idt[256];
_Static_assert(sizeof(struct idt_gate) == 8, "x86 IDT gate size");
_Static_assert(sizeof(struct idt_pointer) == 6, "x86 IDTR operand size");
_Static_assert(offsetof(struct exception_frame, vector) == 32, "ISR frame layout");
_Static_assert(offsetof(struct exception_frame, eip) == 40, "CPU frame layout");
_Static_assert(sizeof(struct exception_frame) == 52, "ring-0 exception frame size");

static const char *const exception_names[32] = {
    "Divide error", "Debug", "NMI", "Breakpoint", "Overflow", "Bound range exceeded",
    "Invalid opcode", "Device not available", "Double fault", "Legacy coprocessor overrun",
    "Invalid TSS", "Segment not present", "Stack segment fault", "General protection",
    "Page fault", "Reserved", "x87 floating point", "Alignment check", "Machine check",
    "SIMD floating point", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved",
    "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved", "Reserved"
};

void idt_init(void)
{
    for (uint32_t vector = 0; vector < 32; ++vector) {
        uintptr_t address = (uintptr_t)isr_entries[vector];
        idt[vector].offset_low = (uint16_t)address;
        idt[vector].offset_high = (uint16_t)(address >> 16);
        idt[vector].selector = 0x08;
        idt[vector].zero = 0;
        idt[vector].attributes = 0x8e; /* Present, DPL 0, 32-bit interrupt gate. */
    }
    uintptr_t syscall_address = (uintptr_t)syscall_entry;
    idt[128].offset_low = (uint16_t)syscall_address;
    idt[128].offset_high = (uint16_t)(syscall_address >> 16);
    idt[128].selector = 0x08;
    idt[128].attributes = 0xee; /* Only this gate permits software INT from DPL 3. */
    uintptr_t irq_address = (uintptr_t)timer_irq_entry;
    idt[32].offset_low = (uint16_t)irq_address;
    idt[32].offset_high = (uint16_t)(irq_address >> 16);
    idt[32].selector = 0x08;
    idt[32].attributes = 0x8e;
    irq_address = (uintptr_t)keyboard_irq_entry;
    idt[33].offset_low = (uint16_t)irq_address;
    idt[33].offset_high = (uint16_t)(irq_address >> 16);
    idt[33].selector = 0x08;
    idt[33].attributes = 0x8e; /* DPL0: user INT 0x21 is forbidden. */
    const struct idt_pointer pointer = {sizeof(idt) - 1, (uintptr_t)idt};
    __asm__ volatile ("lidt %0" : : "m"(pointer) : "memory");
}

static void field(const char *name, uint32_t value)
{
    serial_write(name);
    serial_hex(value);
    serial_write("\n");
}

_Noreturn void exception_dispatch(struct exception_frame *frame)
{
    uint32_t fault_address;
    /* Read CR2 before any diagnostic operation could cause another #PF. */
    __asm__ volatile ("mov %%cr2, %0" : "=r"(fault_address));
    if ((frame->cs & 3) == 3 && frame->vector != 2 && frame->vector != 8 && frame->vector != 18) {
        domain_fault(frame, frame->vector == 14 ? fault_address : 0);
    }
    serial_write("EXCEPTION\n");
    serial_write(frame->vector < 32 ? exception_names[frame->vector] : "Unknown vector");
    serial_write("\n");
    field("vector=", frame->vector);
    field("error=", frame->error);
    field("eip=", frame->eip);
    field("cs=", frame->cs);
    field("eflags=", frame->eflags);
    field("eax=", frame->eax);
    field("ebx=", frame->ebx);
    field("ecx=", frame->ecx);
    field("edx=", frame->edx);
    field("esi=", frame->esi);
    field("edi=", frame->edi);
    field("ebp=", frame->ebp);
    /* saved_esp points at vector; the interrupted ring-0 ESP was 20 bytes above it. */
    field("esp=", frame->saved_esp + 20);
    if (frame->vector == 14) {
        field("cr2=", fault_address);
        serial_write((frame->error & 1) ? "PF: protection " : "PF: not-present ");
        serial_write((frame->error & 2) ? "write " : "read ");
        serial_write((frame->error & 4) ? "user\n" : "supervisor\n");
        if (frame->error & 8) {
            serial_write("PF: reserved page-table bit set\n");
        }
    }
    panic("unrecoverable CPU exception");
}
