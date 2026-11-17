/* SPDX-License-Identifier: GPL-3.0-or-later */
/* A first-free bitmap over fixed-size objects: application policy stays here. */
#include "pool.h"
static int32_t page_call(uint32_t number,struct resource_handle h,uint32_t value,uint32_t rights)
{
    return liberums_call(number,h.index,h.generation,value,rights,0);
}
int pool_check(struct object_pool *pool)
{
    if (!pool->owned || !pool->mapped) { return RES_ESTALE; }
    int32_t result=page_call(SYS_PAGE_QUERY,pool->handle,0,0);
    return result<0 ? result : 0; /* A successful QUERY is not a pin. */
}
int pool_open(struct object_pool *pool)
{
    if (pool->owned) { return RES_EBUSY; }
    uint32_t generation=0;
    int32_t index=liberums_call(SYS_PAGE_CLAIM,RESOURCE_READ|RESOURCE_WRITE|RESOURCE_MAP|RESOURCE_RELEASE|RESOURCE_QUERY,0,0,0,&generation);
    if (index<1) { return index; }
    pool->handle=(struct resource_handle){(uint32_t)index,generation};pool->owned=1;
    pool->address=RESOURCE_WINDOW_END-LIBERUMS_PAGE_SIZE;
    int32_t error=page_call(SYS_PAGE_MAP,pool->handle,pool->address,3);
    if (error) {
        int32_t cleanup=page_call(SYS_PAGE_RELEASE,pool->handle,0,0);
        if (!cleanup) { pool->owned=0; }
        return cleanup ? cleanup : error;
    }
    pool->mapped=1;pool->used[0]=pool->used[1]=pool->live=0;
    volatile uint32_t *words=(void *)(uintptr_t)pool->address;
    for (uint32_t i=0;i<LIBERUMS_PAGE_SIZE/sizeof(*words);++i) {
        if (words[i]!=0) { return RES_EINVAL; }
    }
    return 0;
}
void *pool_take(struct object_pool *pool)
{
    if (pool_check(pool)) { return 0; }
    for (uint32_t i=0;i<OBJECT_COUNT;++i) {
        uint32_t bit=1u<<(i%32);
        if (!(pool->used[i/32]&bit)) {
            pool->used[i/32]|=bit;++pool->live;
            return (void *)(uintptr_t)(pool->address+i*OBJECT_BYTES);
        }
    }
    return 0;
}
int pool_give(struct object_pool *pool,void *object)
{
    int32_t error=pool_check(pool);
    if (error) { return error; }
    uintptr_t address=(uintptr_t)object;
    if (address<pool->address || address-pool->address>=LIBERUMS_PAGE_SIZE
        || (address-pool->address)%OBJECT_BYTES) { return RES_EINVAL; }
    uint32_t slot=(uint32_t)(address-pool->address)/OBJECT_BYTES,bit=1u<<(slot%32);
    if (!(pool->used[slot/32]&bit)) { return RES_EINVAL; }
    pool->used[slot/32]&=~bit;--pool->live;return 0;
}
int pool_close(struct object_pool *pool)
{
    if (pool->live) { return RES_EBUSY; }
    int error=liberums_call(SYS_PAGE_UNMAP,pool->address,0,0,0,0);
    if (error) { return error; }
    pool->mapped=0;
    error=page_call(SYS_PAGE_RELEASE,pool->handle,0,0);
    if (!error) { pool->owned=0; }
    return error;
}
int pool_ack_idle(struct object_pool *pool)
{
    if (pool->live) { return RES_EBUSY; } /* No migration or transparent recovery. */
    uint32_t request=0,fields[4];
    int error=liberums_call(SYS_REVOKE_POLL,0,0,0,0,&request);
    if (error) { return error; }
    for (uint32_t i=0;i<4;++i) {
        error=liberums_call(SYS_REVOKE_INFO,request,i,0,0,&fields[i]);
        if (error) { return error; }
    }
    if (fields[0]!=pool->handle.index || fields[1]!=pool->handle.generation) { return RES_ESTALE; }
    error=page_call(SYS_REVOKE_ACK,pool->handle,request,0);
    if (!error) { pool->owned=pool->mapped=0; }
    return error;
}
