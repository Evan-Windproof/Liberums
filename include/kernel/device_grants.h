/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_DEVICE_GRANTS_H
#define LIBERUMS_DEVICE_GRANTS_H
#include <stdint.h>
#define DEVICE_VGA_READ 1u
#define DEVICE_VGA_WRITE 2u
#define DEVICE_KEYBOARD 4u
struct domain;
/* Trusted internal calls, never authority derived from ELF bytes. */
int32_t device_grants_apply(struct domain *owner, uint32_t requested);
void device_grants_revoke(struct domain *owner, uint32_t requested);
#endif
