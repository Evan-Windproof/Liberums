/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_PIC_H
#define LIBERUMS_PIC_H
#include <stdbool.h>
void pic_keyboard_mask(bool masked);
void pic_keyboard_eoi(void);
void pic_init_timer(void);
void pic_mask_all(void);
void pic_timer_eoi(void);
#endif
