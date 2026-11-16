/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_DOMAIN_POOL_H
#define LIBERUMS_DOMAIN_POOL_H
#include <kernel/domain.h>
#include <stddef.h>
/* Trusted, bounded lifecycle API. Not fork/exec or a user-visible spawn service. */
int32_t domain_reserve(struct domain **result);
int32_t domain_create_external(const uint8_t *image, size_t length, uint32_t devices,
                               struct domain **result);
int32_t domain_reap(struct domain *domain);
void domain_cleanup(struct domain *domain);
bool domain_is_current(const struct domain *domain);
#endif
