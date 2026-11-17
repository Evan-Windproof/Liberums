# Lesson 3 — 获得和归还一页

内核返回的是句柄，不是可以直接解引用的 PFN。
[pool_open](../../examples/libos/memory-policy/pool.c) 是可执行的完整错误/回滚例子。

```c
uint32_t generation;
int32_t index = liberums_call(SYS_PAGE_CLAIM,
    RESOURCE_READ|RESOURCE_WRITE|RESOURCE_MAP|RESOURCE_RELEASE|RESOURCE_QUERY,
    0,0,0,&generation);
if (index < 1) { return index; }
int32_t error = liberums_call(SYS_PAGE_MAP,(uint32_t)index,generation,
    RESOURCE_WINDOW_END-LIBERUMS_PAGE_SIZE,RESOURCE_READ|RESOURCE_WRITE,0);
```

成功后该 VA 才是有效内存。QUERY 返回 PFN/rights，但不是 pin；它不保证下一条用户 load 前不会被撤销。
先 UNMAP 去掉绑定，再 RELEASE 归还拥有权；RELEASE 自身也会撤除所有 alias。
MAP 失败仍可能拥有 CLAIM 的页，需要补偿 RELEASE；补偿失败不能忘记身份并宣称清理成功。

`make run-libos LIBOS_ELF=build/examples/memory-policy/libos.elf` 实际申请、检查初始零页、写入、验证、
UNMAP、RELEASE，随后旧 QUERY 必须 ESTALE。`make test-libos-examples` 再核对实际 PTE、PFN/owner/epoch 与清零。

常见错误：没有 MAP 就访问 PFN、VA 不对齐/超出窗口、释放后还使用旧指针、申请错误权限。
内核页表由 Ring 0 管理，不能把自己构造的 PTE 交进去。
