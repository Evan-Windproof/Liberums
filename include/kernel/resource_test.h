/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef LIBERUMS_RESOURCE_TEST_H
#define LIBERUMS_RESOURCE_TEST_H
#include <kernel/resource_abi.h>
enum resource_test_mode resource_test_select(const char *args);
void resource_test_pressure_begin(enum resource_test_mode mode);
void resource_test_pressure_end(void);
#endif
