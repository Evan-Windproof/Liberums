# 单 CPU 时间片与安全恢复

## 一次轮转

`scheduler.c` 顺序检查域 1、域 2 的 READY 状态。`domain_run_slice()` 设置 RUNNING、
TSS.ESP0、私有 CR3，再通过 `arch_user_enter(context, segments)` 进入用户。
用户可 yield、exit、触发异常，或被一个 PIT tick 打断。每次返回调度栈后先恢复内核 CR3，
再处理期限、诊断策略和终止清理。下一 READY 域获得一次执行机会，终止槽跳过，全部终止即返回。
持续触发异常的域在第一次不可恢复异常时就成为 FAULTED，不会反复重试同一条指令。

每片为一个**实际交付的 IRQ tick**。没有线程、优先级、抢占内核、实时调度或 CPU 时长记账。
若用户主动 yield，片可以更短。不能把它解释为每域获得精确 10 ms 墙钟时间的保证。

## 保存和恢复的保护检查

中断入口确认 CS=0x1b、当前 RUNNING 域、CR3 等于该域页目录、现场地址在可信栈内、
外层 SS=0x23。保存 GP、EIP/CS/EFLAGS、用户 ESP/SS，以及 DS/ES/FS/GS。
恢复时只允许现有 GDT 的 null、用户 code/data 描述符（分别为 0..3、0x18..0x1b、0x20..0x23），CS/SS 由可信初始化控制。
保留算术标志和 DF；清理 TF、RF、NT、VM、AC、IOPL 等，并按当前模式设置 IF。
用户无法通过伪造 EFLAGS 提升 I/O 权限。保存的 RF 可能来自 GDB 硬件断点，恢复测试按公开安全掩码验证，
并逐项比对其余寄存器。不同 RPL 的用户描述符仍是 CPU 合法加载的值，不可因此触发内核 Panic。
诊断使用 DS=3（null）、ES=0x20、FS=0x21、GS=0x18；IRQ 保存原始值，
本次 QEMU 的跨特权级 IRET 将 unusable 的 null DS 归一为 0，其余非空选择子连同 RPL 精确恢复。
null 的两种表示均不可用于访存；测试明确验证这个归一，而非要求硬件保留无意义的 null RPL 位。
普通 syscall 继续沿用旧 ABI，将数据段归一为 0x23；异步 IRQ 保存真实段值。

每次进入用户设置 CR0.TS，继续拒绝未虚拟化的 x87/MMX/SIMD，避免跨域泄漏扩展寄存器。
中断 C 使用 soft-float，不触碰这些寄存器。

可信 `scheduler_esp` 属于单 CPU 调度调用，保存的是内核调用栈；用户页或 ESP 不能覆盖它。
只有回到这条栈且恢复内核 CR3 后，才执行终止后的空间销毁。
每域内核栈是内核静态对象，永不作为可撤销资源释放。活动页表也不在资源 capability 中。

## Tick 与期限

`tick.c` 使用 boot-relative uint32 单调数，不做模数比较。期限为 `now + delay`，delay 必须非零，
加法溢出返回 EOVERFLOW 且不修改资源或事件。绝对期限仅指这个 tick 域，不是 UTC 或硬实时期限。
PIC 不能排队任意多个被屏蔽的边沿；IF=0 期间可能合并 tick，GDB 暂停也影响时间。
因此它是交付计数，不是可靠墙钟。当前内核操作有界，但不承诺最坏中断延迟。

约 497 天连续 100 Hz 计数到 UINT32_MAX 时，定时器屏蔽 IRQ，计数不回绕。
调度者处理到期请求，随后将 READY 域终止为 EOVERFLOW，并在再进入用户前同样检查耗尽，
防止“已屏蔽时钟后又进入无限循环”。不会让旧期限或旧身份因回绕重新生效。
请求 ID 也不回绕，耗尽拒绝新请求。重启才重置计数；未实现持久化或长时钟扩展。

## 兼容模式

默认 `make run` 与 `QEMU_APPEND=external` 保持合作顺序和 IF=0。
`preempt` 为显式执行语义选择；两种模式都报告 ABI 1.2，但特性位不同：

| 模式 | Feature Flags | COOPERATIVE | PREEMPTION / REVOCATION |
| --- | --- | --- | --- |
| 默认兼容 | 31 (0x1f) | 有 | 无 |
| 显式抢占 | 119 (0x77) | 无 | 有 |

旧程序要求 COOPERATIVE 时，只在默认模式加载。要求新能力的程序只能在新模式加载。
旧 SDK、ELF 无需重编译即可继续走原模式；不保证旧程序在新执行语义下也兼容。
