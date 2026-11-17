/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TEACHING_LIBOS_RUNTIME_H
#define TEACHING_LIBOS_RUNTIME_H
#include <abi/elf.h>
struct libos_info { uint32_t version,features,quota,bindings; };
extern struct libos_info libos_info;
/* This application's API is our choice, not another kernel syscall. */
int libos_main(void);
int libos_log(const char *message);
void libos_yield(void);
_Noreturn void libos_exit(uint32_t status);
void example_observe(uint32_t stage,uint32_t value);
extern volatile uint32_t example_stage,example_value,example_sequence;
#endif
