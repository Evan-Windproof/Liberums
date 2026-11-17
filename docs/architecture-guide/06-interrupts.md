# 6. CPU、中断与现场

IDT 决定异常/中断入口；DPL3 只开放 syscall gate。PIT 产生 IRQ0，PIC 需要恰好一次 EOI。
PS/2 的 IRQ1 只收原始字节，不替用户编辑命令。

```text
user -> IRQ0 -> save GP/segments/CPU frame -> EOI -> READY
     -> trusted scheduler stack -> kernel CR3 -> next READY -> TSS/CR3 -> iret
```

[irq.S](../../arch/x86/irq.S)、[timer.c](../../kernel/timer.c)、
[scheduler.c](../../kernel/scheduler.c)、[user_context.S](../../arch/x86/user_context.S)。
C 中断处理前必须清 DF 并使用可信段；保存/恢复算术标志和合法用户段也很重要。

```c
/* 用户仍可主动让出，但不能决定特权调度或自行关中断。 */
liberums_call(SYS_YIELD, 0, 0, 0, 0, 0);
```

实验：`make test-phase8` 的四域哨兵循环检查真实 IRQ0 前后的 GP、段、ESP、EFLAGS、CR3/TSS；
`make test-libos-examples` 检查普通 C bitmap 工作负载被抢占后数据仍正确。
常见错误：把 YIELD 当硬件抢占证据，或丢 EOI 导致后续中断停止。
局限：单 CPU、内核 C 不抢占；合作 profile 的 IF=0，不保证不合作程序能被强制收回。tick 不是墙钟，也没有实时延迟保证。
