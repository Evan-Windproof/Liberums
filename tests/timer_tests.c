/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <kernel/tick.h>
#include <assert.h>
#include <stdio.h>
int main(void)
{
    unsigned checks = 0;
#define CHECK(x) do { assert(x); ++checks; } while (0)
    uint32_t tick = 0, deadline = 77;
    CHECK(tick_advance(&tick) && tick == 1);
    CHECK(!tick_deadline(10, 0, &deadline) && deadline == 77);
    CHECK(tick_deadline(10, 5, &deadline) && deadline == 15);
    CHECK(tick_deadline(UINT32_MAX - 1, 1, &deadline) && deadline == UINT32_MAX);
    CHECK(!tick_deadline(UINT32_MAX - 1, 2, &deadline) && deadline == UINT32_MAX);
    CHECK(!tick_deadline(UINT32_MAX, 1, &deadline));
    tick = UINT32_MAX - 1;
    CHECK(tick_advance(&tick) && tick == UINT32_MAX);
    CHECK(!tick_advance(&tick) && tick == UINT32_MAX);
    for (uint32_t now = 0; now < 1000; ++now) {
        CHECK(tick_deadline(now, 100, &deadline) && deadline > now && deadline - now == 100);
    }
    printf("PASS: timer boundary assertions=%u\n", checks);
}
