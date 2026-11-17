# 1. Exokernel 与 LibOS

程序要分配一个 64 字节对象，最终需要内存，但“如何选 64 字节块”不需要特权。
Liberums 校验用户是否拥有物理页、是否有映射权限；bitmap、first-fit、best-fit 都由用户程序选择。

| 架构 | 通常的责任安排 |
|---|---|
| Monolithic | 许多驱动、文件/网络服务在特权地址空间内协作 |
| Microkernel | 特权核心保持精简，许多服务作为独立进程通过通信提供 |
| Exokernel | 核心保护和仲裁资源，Library OS 在用户域实现高层管理与 API |
| Unikernel | 常将一个应用与所需库组成专用映像；不等于隔离多个 Ring 3 LibOS 的 Exokernel |

MIT Exokernel、Xok/ExOS 是相关思想的历史背景。Liberums 不是其移植，没有继承其代码、性能结论或全部机制。

```text
app -> own bitmap allocator -> PAGE_CLAIM/MAP -> kernel ownership checks -> MMU
```

真实对应：[用户策略](../../examples/libos/memory-policy/pool.c)、
[保护检查](../../kernel/resource.c)、[MMU 更新](../../arch/x86/paging.c)。

```c
/* 用户态选对象；这里不需要一次分配对应一次 syscall。 */
void *object = pool_take(&pool);
```

实验：`make test-libos-examples` 中两种对象大小共用同一个内核；
在 GDB `example_observed` 停点查看 `current_domain->id` 与 `$cr3`。
常见误解：内核有空闲页表或 Round-robin，并不意味着应用堆策略进了内核；保护仍必须仲裁竞争。
局限：本项目只暴露有限页/CPU/设备机制，没有通用存储、网络、用户 pager 或完整应用生态。
