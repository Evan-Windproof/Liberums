/* SPDX-License-Identifier: GPL-3.0-or-later */
/* Real slot/lifecycle implementation; ELF, hardware and RAM endpoints mocked.
 * QEMU suite independently verifies the actual parser, pages, traps and IRQs. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <kernel/domain_pool.h>
#include <kernel/elf_loader.h>
static struct domain slots[DOMAIN_CAPACITY];
static bool resource_busy[DOMAIN_CAPACITY], event_busy[DOMAIN_CAPACITY], device_busy[DOMAIN_CAPACITY];
static bool oom, grant_fail;
static uint32_t outstanding;
static size_t index_of(const struct domain *d) { return (size_t)(d-slots); }
struct domain *domain_slot(uint32_t id) { assert(id>=1 && id<=DOMAIN_CAPACITY);return &slots[id-1]; }
bool domain_is_current(const struct domain *d) { (void)d;return false; }
uint32_t abi_features(void) { return LIBERUMS_PREEMPT_FEATURES; }
int32_t elf_validate_features(const uint8_t *p,size_t n,struct elf_plan *plan,uint32_t features)
{
    (void)features;
    if (!p || n!=1 || *p!=1) { return RES_ENOEXEC; }
    memset(plan,0,sizeof(*plan));plan->entry=0x08000000;return 0;
}
int32_t address_space_create_external(struct address_space *s,const uint8_t *p,const struct elf_plan *plan)
{
    (void)p;(void)plan;
    s->count=3;outstanding+=3;s->external=true;
    if (oom) { return RES_ENOMEM; }
    s->live=true;return 0;
}
void address_space_destroy(struct address_space *s)
{
    assert(outstanding>=s->count);outstanding-=s->count;
    s->count=0;s->live=false;s->external=false;
}
void domain_initialize(struct domain *d,uint32_t id,uint32_t entry,uint32_t top)
{
    assert(d==domain_slot(id) && d->epoch!=UINT32_MAX);
    ++d->epoch;d->id=id;d->state=DOMAIN_READY;d->context.cpu.eip=entry;d->context.user_esp=top-4;
}
int32_t device_grants_apply(struct domain *d,uint32_t devices)
{
    if (grant_fail) { return RES_EBUSY; }
    device_busy[index_of(d)]=devices!=0;return 0;
}
bool resource_domain_busy(const struct domain *d) { return resource_busy[index_of(d)]; }
bool revoke_domain_busy(const struct domain *d) { return event_busy[index_of(d)]; }
bool keyboard_domain_busy(const struct domain *d) { return device_busy[index_of(d)]; }
bool vga_device_domain_busy(const struct domain *d) { return device_busy[index_of(d)]; }
void keyboard_revoke(struct domain *d) { device_busy[index_of(d)]=false; }
void vga_device_revoke(struct domain *d) { device_busy[index_of(d)]=false; }
void resource_cleanup(struct domain *d)
{
    resource_busy[index_of(d)]=event_busy[index_of(d)]=false;
    d->resource_count=0;d->inbox=(struct resource_handle){0,0};
}
_Noreturn void panic(const char *m) { puts(m);abort(); }
int main(void)
{
    const uint8_t valid=1,bad=0;
    struct domain *d,*live[DOMAIN_CAPACITY];
    assert(domain_create_external(&bad,1,0,&d)==RES_ENOEXEC && d==NULL && outstanding==0);
    oom=true;assert(domain_create_external(&valid,1,0,&d)==RES_ENOMEM && outstanding==0);oom=false;
    grant_fail=true;assert(domain_create_external(&valid,1,7,&d)==RES_EBUSY && outstanding==0);grant_fail=false;
    for (unsigned i=0;i<DOMAIN_CAPACITY;++i) {
        assert(domain_create_external(&valid,1,0,&live[i])==0 && live[i]->id==i+1);
        assert(domain_reap(live[i])==RES_EBUSY);
    }
    assert(domain_create_external(&valid,1,0,&d)==RES_ENOSPC && d==NULL);
    for (unsigned i=0;i<DOMAIN_CAPACITY;++i) {
        live[i]->state=DOMAIN_EXITED;assert(domain_reap(live[i])==0);
    }
    assert(outstanding==0);
    for (unsigned round=0;round<64;++round) {
        uint32_t old=slots[0].epoch;
        assert(domain_create_external(&valid,1,7,&d)==0 && d->id==1 && d->epoch==old+1);
        memset(d->kernel_stack,0xa5,sizeof(d->kernel_stack));
        d->resource_count=1;resource_busy[0]=event_busy[0]=true;
        d->inbox=(struct resource_handle){1,1};d->state=DOMAIN_FAULTED;
        assert(domain_reap(d)==0 && d->state==DOMAIN_UNUSED && outstanding==0);
        for (size_t j=0;j<sizeof(d->kernel_stack);++j) { assert(d->kernel_stack[j]==0); }
        assert(!resource_busy[0] && !event_busy[0] && !device_busy[0]);
    }
    resource_busy[0]=true;
    for (unsigned i=1;i<DOMAIN_CAPACITY;++i) { slots[i].state=DOMAIN_PREPARING; }
    assert(domain_reserve(&d)==RES_EBUSY && d==NULL);resource_busy[0]=false;
    for (unsigned i=0;i<DOMAIN_CAPACITY;++i) { slots[i].state=DOMAIN_UNUSED;slots[i].epoch=UINT32_MAX; }
    assert(domain_reserve(&d)==RES_EOVERFLOW && d==NULL);
    assert(domain_reap(NULL)==RES_EINVAL);
    puts("PASS: pool capacity/ID, rollback, live-slot exclusion, 64 reuses, stack/context cleanup, retained authority and epoch exhaustion");
}
