/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_KEYBOARD_H
#define LIBERUMS_KEYBOARD_H
#include <abi/keyboard.h>
#include <stdbool.h>
struct domain;
bool keyboard_start(void);
void keyboard_diagnostic(void);
bool keyboard_grant(struct domain *owner);
int32_t keyboard_info(uint32_t device, uint32_t *format);
int32_t keyboard_acquire(struct domain *owner, uint32_t device, struct resource_handle *handle);
int32_t keyboard_poll(struct domain *owner, struct resource_handle handle, uint32_t *byte);
int32_t keyboard_stats(struct domain *owner, struct resource_handle handle, uint32_t *lost);
int32_t keyboard_release(struct domain *owner, struct resource_handle handle);
void keyboard_revoke(struct domain *owner);
#endif
