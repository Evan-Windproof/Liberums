/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_PHASE72_TEST_H
#define LIBERUMS_PHASE72_TEST_H
struct domain;
void phase72_run(const char *args);
void phase72_step(struct domain *domain);
#endif
