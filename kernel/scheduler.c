/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/domain.h>
#include <kernel/resource.h>
#include <kernel/resource_test.h>
#include <kernel/timer.h>
#include <kernel/revoke.h>
#include <kernel/phase6_test.h>
/* One delivered PIT tick per slice (~10 ms), single CPU, no kernel preemption.
 * A terminated slot is skipped; all terminal -> return to lifetime owner. */
void domain_schedule(void)
{
    bool runnable;
    do {
        runnable = false;
        for (uint32_t id = 1; id <= 2; ++id) {
            struct domain *domain = domain_slot(id);
            if (domain->state != DOMAIN_READY) {
                continue;
            }
            if (timer_exhausted()) {
                domain->exit_status = (uint32_t)RES_EOVERFLOW;
                domain->state = DOMAIN_EXITED;
                resource_cleanup(domain);
                continue;
            }
            runnable = true;
            domain_run_slice(domain);
            resource_test_pressure_end();
            revoke_expire(timer_now());
            phase6_test_step(domain);
            if (timer_exhausted() && domain->state == DOMAIN_READY) {
                domain->exit_status = (uint32_t)RES_EOVERFLOW;
                domain->state = DOMAIN_EXITED;
            }
            if (domain->state == DOMAIN_EXITED || domain->state == DOMAIN_FAULTED) {
                resource_cleanup(domain);
            }
        }
    } while (runnable);
}
