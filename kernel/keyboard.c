/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/keyboard.h>
#include <kernel/domain.h>
#include <kernel/timer.h>
#include <kernel/serial.h>
#include <arch/x86/ps2.h>
#include <arch/x86/pic.h>
#include <arch/x86/irq.h>

/* Single CPU, all entry points run with IF=0: IRQ producer and syscall consumer
 * cannot race. No untrusted pointer, scan-code interpretation, or editor here. */
static struct {
    struct capability cap;
    uint32_t grant_id, grant_epoch;
    uint32_t head, count, dropped, lost;
    uint8_t bytes[KBD_QUEUE_CAPACITY];
    bool active, overflow, enabled;
} keyboard;
static volatile uint32_t keyboard_irqs, keyboard_eois, keyboard_bytes, keyboard_bad;
static void increment(uint32_t *value)
{
    if (*value != UINT32_MAX) { ++*value; }
}
static void clear_queue(void)
{
    for (uint32_t i = 0; i < KBD_QUEUE_CAPACITY; ++i) { keyboard.bytes[i] = 0; }
    keyboard.head = keyboard.count = keyboard.dropped = 0;
    keyboard.overflow = false;
}
bool keyboard_start(void)
{
    if (!timer_enabled() || keyboard.grant_id != 0) { return false; }
    pic_keyboard_mask(true);
    keyboard.enabled = false;
    if (!ps2_start()) { return false; }
    clear_queue();
    keyboard.enabled = true;
    pic_keyboard_mask(false);
    return true;
}
void keyboard_abort_start(void)
{
    /* Roll back only an unowned initialization, never somebody else's grant. */
    if (keyboard.grant_id != 0) { return; }
    pic_keyboard_mask(true);
    keyboard.enabled = keyboard.active = false;
    clear_queue();
    ps2_stop();
}
bool keyboard_grant(struct domain *owner)
{
    if (!keyboard.enabled || domain_lookup(owner->id) != owner || keyboard.grant_id != 0) { return false; }
    keyboard.grant_id = owner->id;
    keyboard.grant_epoch = owner->epoch;
    return true;
}
int32_t keyboard_info(uint32_t device, uint32_t *format)
{
    if (!keyboard.enabled || device != KBD_DEVICE_PS2) { return RES_ENOENT; }
    *format = KBD_SCANSET1;
    return KBD_QUEUE_CAPACITY;
}
int32_t keyboard_acquire(struct domain *owner, uint32_t device, struct resource_handle *handle)
{
    if (device != KBD_DEVICE_PS2) { return RES_EINVAL; }
    if (!keyboard.enabled || keyboard.grant_id != owner->id || keyboard.grant_epoch != owner->epoch) {
        return RES_EPERM;
    }
    if (keyboard.active) { return RES_EBUSY; }
    uint32_t generation;
    if (!capability_next(keyboard.cap.generation, &generation)) { return RES_EOVERFLOW; }
    clear_queue(); /* Nothing typed before this acquisition is inherited. */
    keyboard.lost = 0;
    keyboard.cap = (struct capability){generation, owner->id, owner->epoch, RESOURCE_READ};
    keyboard.active = true;
    *handle = (struct resource_handle){KBD_HANDLE, generation};
    return 0;
}
static int32_t resolve(struct domain *owner, struct resource_handle handle)
{
    if (!keyboard.active || handle.index != KBD_HANDLE) { return RES_ESTALE; }
    return capability_check(&keyboard.cap, handle.generation, owner->id, owner->epoch, RESOURCE_READ);
}
int32_t keyboard_poll(struct domain *owner, struct resource_handle handle, uint32_t *byte)
{
    int32_t error = resolve(owner, handle);
    if (error != 0) { return error; }
    if (keyboard.overflow) {
        *byte = keyboard.dropped;
        /* Count retained bytes discarded to avoid delivering an incomplete
         * modifier/prefix sequence. lost is also saturating. */
        for (uint32_t i = 0; i < keyboard.count; ++i) {
            increment(byte);
            increment(&keyboard.lost);
        }
        clear_queue();
        return RES_EOVERFLOW;
    }
    if (keyboard.count == 0) { return RES_EAGAIN; }
    *byte = keyboard.bytes[keyboard.head];
    keyboard.bytes[keyboard.head] = 0;
    keyboard.head = (keyboard.head + 1) % KBD_QUEUE_CAPACITY;
    --keyboard.count;
    return 0;
}
int32_t keyboard_stats(struct domain *owner, struct resource_handle handle, uint32_t *lost)
{
    int32_t error = resolve(owner, handle);
    if (error != 0) { return error; }
    *lost = keyboard.lost;
    return (int32_t)keyboard.count;
}
int32_t keyboard_release(struct domain *owner, struct resource_handle handle)
{
    int32_t error = resolve(owner, handle);
    if (error == 0) {
        clear_queue();
        keyboard.active = false;
        keyboard.cap.owner_id = keyboard.cap.owner_epoch = keyboard.cap.rights = 0;
    }
    return error;
}
void keyboard_revoke(struct domain *owner)
{
    if (keyboard.grant_id != owner->id || keyboard.grant_epoch != owner->epoch) { return; }
    pic_keyboard_mask(true);
    keyboard.enabled = keyboard.active = false;
    keyboard.grant_id = keyboard.grant_epoch = 0;
    keyboard.cap.owner_id = keyboard.cap.owner_epoch = keyboard.cap.rights = 0;
    clear_queue();
    ps2_stop();
}
static void enqueue(uint8_t byte)
{
    if (!keyboard.enabled || !keyboard.active) { return; } /* Drop unowned input. */
    if (keyboard.overflow || keyboard.count == KBD_QUEUE_CAPACITY) {
        keyboard.overflow = true;
        increment(&keyboard.dropped);
        increment(&keyboard.lost);
        return;
    }
    keyboard.bytes[(keyboard.head + keyboard.count) % KBD_QUEUE_CAPACITY] = byte;
    ++keyboard.count;
}
void keyboard_dispatch(struct irq_frame *frame)
{
    (void)frame; /* IRQ1 never schedules or changes the interrupted context. */
    if (keyboard_irqs != UINT32_MAX) { ++keyboard_irqs; }
    for (uint32_t i = 0; i < 32; ++i) {
        uint8_t byte;
        enum ps2_result result = ps2_read(&byte);
        if (result == PS2_EMPTY) { break; }
        if (result == PS2_DISCARD || byte == 0 || byte == 0xff) {
            if (keyboard_bad != UINT32_MAX) { ++keyboard_bad; }
            if (keyboard.active) {
                keyboard.overflow = true;
                increment(&keyboard.dropped);
                increment(&keyboard.lost);
            }
        } else {
            if (keyboard_bytes != UINT32_MAX) { ++keyboard_bytes; }
            enqueue(byte);
        }
    }
    pic_keyboard_eoi();
    if (keyboard_eois != UINT32_MAX) { ++keyboard_eois; }
}

__attribute__((noinline)) void keyboard_diagnostic_done(void)
{
    __asm__ volatile ("" : : : "memory");
}
void keyboard_diagnostic(void)
{
    serial_write("Keyboard diagnostic ready\n");
    while (keyboard_bytes == 0) { __asm__ volatile ("sti; hlt; cli" : : : "memory"); }
    keyboard_diagnostic_done();
    serial_write("Keyboard IRQ1 diagnostic completed\n");
}
