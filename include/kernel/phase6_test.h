/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_PHASE6_TEST_H
#define LIBERUMS_PHASE6_TEST_H
#include <stdbool.h>
struct domain;
bool phase6_test_selected(const char *args);
void phase6_test_run(const char *args);
void phase6_test_step(struct domain *domain);
#endif
