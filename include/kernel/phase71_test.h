/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_PHASE71_TEST_H
#define LIBERUMS_PHASE71_TEST_H
struct domain;
void phase71_test_run(const char *args);
void phase71_test_step(struct domain *domain);
#endif
