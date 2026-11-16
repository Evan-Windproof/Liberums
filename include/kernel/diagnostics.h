/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_DIAGNOSTICS_H
#define LIBERUMS_DIAGNOSTICS_H
struct domain;
void diagnostics_init(const char *args);
void diagnostics_domain_step(struct domain *domain);
#endif
