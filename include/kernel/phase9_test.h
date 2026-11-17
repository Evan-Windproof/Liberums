/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_PHASE9_TEST_H
#define LIBERUMS_PHASE9_TEST_H
struct boot_context;
struct domain;
void phase9_test_init(const struct boot_context *boot);
void phase9_test_step(struct domain *domain);
#endif
