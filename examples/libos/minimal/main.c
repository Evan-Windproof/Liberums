/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "runtime.h"
int libos_main(void)
{
    example_observe(1,libos_info.version);
    return libos_log("Hello from my independent LibOS\n") ? 92 : 0;
}
