/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_DIAGNOSTICS_H
#define LIBERUMS_DIAGNOSTICS_H
struct domain;
struct boot_context;
void diagnostics_init(const struct boot_context *boot);
void diagnostics_domain_step(struct domain *domain);
#endif
