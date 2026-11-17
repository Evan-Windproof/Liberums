/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Keep optional experiment hooks out of scheduler policy. No grant path here. */
#include <kernel/diagnostics.h>
#include <kernel/phase6_test.h>
#include <kernel/phase71_test.h>
#include <kernel/phase72_test.h>
#include <kernel/phase8_test.h>
#include <kernel/phase9_test.h>
#include <kernel/device_grants.h>
#include <kernel/domain.h>
#include <kernel/boot_info.h>
static bool device_test;
static volatile uint32_t diagnostic_revoke;
void diagnostics_init(const struct boot_context *boot)
{
    phase8_test_init(boot);
    phase9_test_init(boot);
    device_test = boot_has_option(boot->commandline, "selftest=device-revoke");
}
void diagnostics_domain_step(struct domain *domain)
{
    phase6_test_step(domain);
    phase71_test_step(domain);
    phase72_step(domain);
    phase8_test_step(domain);
    phase9_test_step(domain);
    if (device_test && domain->id == 1 && diagnostic_revoke != 0) {
        uint32_t mask = diagnostic_revoke;
        diagnostic_revoke = 0;
        device_grants_revoke(domain, mask);
    }
}
