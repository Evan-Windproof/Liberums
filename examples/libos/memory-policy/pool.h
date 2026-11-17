/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef TEACHING_OBJECT_POOL_H
#define TEACHING_OBJECT_POOL_H
#include "runtime.h"
#ifndef OBJECT_BYTES
#define OBJECT_BYTES 64u
#endif
#define OBJECT_COUNT (LIBERUMS_PAGE_SIZE/OBJECT_BYTES)
_Static_assert(OBJECT_BYTES>=64 && LIBERUMS_PAGE_SIZE%OBJECT_BYTES==0,"bounded object size");
struct object_pool {
    struct resource_handle handle;
    uint32_t address,used[2],live;
    int owned,mapped;
};
int pool_open(struct object_pool *pool);
void *pool_take(struct object_pool *pool);
int pool_give(struct object_pool *pool,void *object);
int pool_close(struct object_pool *pool);
int pool_ack_idle(struct object_pool *pool);
int pool_check(struct object_pool *pool);
#endif
