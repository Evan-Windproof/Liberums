# Phase 8：有界 Round-robin

原调度器仍是一个简单 `do/for` 循环，遍历 ID 1～`DOMAIN_CAPACITY`，跳过非 READY 域。
每次 `domain_run_slice` 都确认：没有当前运行域、对象属于实际槽位、状态 READY、地址空间 live。
通过检查后设置当前身份、RUNNING、TSS.ESP0 和独立 CR3，再通过原汇编 `iret` 进入 CPL3。

在 `multi-external` 下，PIT/PIC IRQ0 提供原有约 10ms 的硬件时间片：
保存 CPU outer frame、全部 GP 及段现场，恰好一次 EOI，然后切回调度栈和内核 CR3。
YIELD 主动返回同一调度器；EXIT/FAULT 不会再次运行，资源清理由原机制执行。
一个 READY 域每次获得一个 slice，空槽不占 slice。故障仅终止来源域。
新建 READY 域在下一次扫描其 ID 时得到运行机会；不承诺新建域的墙钟延迟。

内核 C 代码不被抢占，资源操作和生命周期事务始终在单 CPU、IF=0 下进行。
没有内核线程、优先级、实时策略或 SMP；用户态仍可在自己的 slice 内调度协程或线程。
合作兼容 profile 仍允许不合作代码占用 CPU，这是旧 profile 的公开限制。

## 实际恢复证据

四个不合作用户程序将七个 GP 寄存器设为不同哨兵，在 DF/CF 可变的循环中执行。
GDB 在 IRQ0 保存 GP 之前读取真实 CPU outer frame、原寄存器、段、CR3、TSS 栈位置；
等待相同域恢复到被抢占 EIP 后，再逐项比较 GP/段/ESP/安全 EFLAGS/CR3。
每域都必须有实际 preemptions，四个 ID 都必须出现在观测序列中。
允许 x86 调试 RF 的硬件语义，权限相关 IF/IOPL 另行严格断言。

测试不向 timer 计数或保存寄存器写入预期结果，不用 YIELD 冒充 IRQ0。
`timer_irq_capture` 只是已有汇编指令间的符号，不增加新的中断行为。
原 Phase 6 测试继续检查更广泛的选择子、内核来源 IRQ 和计数溢出安全。
原有捕获现场及 CR3/TSS 来源检查保持，不为四域放松任何断言。

PIT tick 是交付的硬件中断计数，不是墙钟。调试暂停会影响时间关系，
本阶段不提供硬实时、公平墙钟份额或饥饿时间上界。
