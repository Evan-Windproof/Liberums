/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_ARCH_X86_TEST_FAULTS_H
#define LIBERUMS_ARCH_X86_TEST_FAULTS_H
#include <stdint.h>

/* Implemented in test_faults.S; call only from explicit boot diagnostics. */
void test_ud(void);
void test_divide(void);
void test_gp(void);
void test_pf_read(void);
void test_pf_null(void);
void test_ro_write(void);
void test_unmapped(uint32_t address);
#endif
