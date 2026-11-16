/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_PHASE8_TEST_H
#define LIBERUMS_PHASE8_TEST_H
struct domain;
struct boot_context;
void phase8_test_init(const struct boot_context *boot);
void phase8_test_step(struct domain *domain);
void phase8_test_before_module(unsigned int ordinal);
void phase8_test_after_module(void);
#endif
