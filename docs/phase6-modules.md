# Phase 6 模块与接口

| 模块 | 职责、主要接口/结构 | 协作和验证 |
| --- | --- | --- |
| arch/x86/pit.c、pit.h | PIT channel 0 周期源，pit_init、PIT_DIVISOR | timer_start 调用；真实 IRQ 连续到达 |
| arch/x86/pic.c、pic.h | 重映射、只开放 IRQ0、EOI、屏蔽 | GDB monitor info pic 核对 IMR、向量和 ISR；IRQ/EOI 计数一致 |
| arch/x86/irq.S、irq.h | 独立 IRQ 入口，segment_context、irq_frame，区分内/外层栈 | 保存再使用寄存器；GDB 对比 CPU 原始帧和 C 帧，内核返回及用户恢复 |
| kernel/tick.c、tick.h | 可测试的单调计数与 checked deadline | timer/revoke 复用；ASan/UBSan 边界测试 |
| kernel/timer.c、timer.h | IRQ 计数、期限时基、耗尽处理、执行 profile | 用户 IRQ EOI 后调用 domain_preempt；内核 IRQ 原位返回 |
| kernel/scheduler.c | 一 tick 轮转、期限处理、终止清理 | 只在 boot 栈和内核 CR3 下运行；A 无 yield 时 B 仍执行 |
| kernel/domain.c、domain.h | 保留域/epoch 生命周期，新增 domain_preempt、domain_run_slice | 验证可信栈/CR3，保存 CPU 与独立段状态；原 60 字节上下文不变 |
| arch/x86/user_context.S | arch_user_enter 接收段状态，恢复安全用户现场 | DS 最后装入；之后仅经 SS pop/iret，允许用户 null DS 和合法的不同 RPL |
| kernel/revoke.c、revoke.h | 两槽/域事件、request/poll/info/ack/expire/cancel | 记录完整句柄/owner epoch/期限/请求号；无用户指针；真实 resource transition |
| kernel/resource.c、resource.h | pending 状态、拒绝新绑定/转移、共同回收路径 | 通过 guarded begin/complete 接口与 revoke 协作；所有别名、TLB、清零/配额 |
| include/abi/event.h、syscall.h | 24..26、字段、原因、1.2 与 profile flags | syscall.c 传整数，不复制用户内存；旧号和返回约定保留 |
| kernel/elf_loader.c、external.c | 按当前 profile 检查 ELF note | pure elf_validate 保留兼容 profile，新增 elf_validate_features；加载预算/回滚不变 |
| tests/phase6_runtime.c、phase6_test.h | 显式诊断模式的内核压力源/生命周期 | 不把成功写死在检查点；每次实际创建、运行、回收两域 |
| tests/fixtures/preempt/ | 一页内置诊断代码，资源/别名/ACK、恶意循环 | 公开 SDK；旧单页链接 ASSERT 继续有效 |
| tests/fixtures/external_user/preempt.c | 独立编译 ELF 的长计算与资源访问 | 1.2 note，真实多次抢占，不链接内核实现 |
| tests/fixtures/compat/ | 实际旧 SDK 1.1 构建 ELF、来源和 SHA256 | 不随 SDK 重建；不能冒充为真实 Aelthuvor 构建 |
| tests/timer_tests.c、revoke_tests.c | 宿主边界与真实协议 C 代码 | 假物理页/PTE 只用于协议边界；硬件同步另由 QEMU 证明 |
| tests/phase6.py | GDB 现场、恢复、协议、页表、复用、兼容测试 | 最多同时四个硬件断点；失败保留日志并非零退出 |

调用关系：

```text
main -> timer_start (仅显式模式) -> domains / external / phase6 diagnostics
  domain_schedule -> domain_run_slice -> arch_user_enter -> Ring 3
  IRQ0 -> timer_dispatch -> EOI -> domain_preempt -> arch_user_leave
  scheduler -> revoke_expire -> resource_revoke_complete -> unbind/TLB/zero/free
  int80 -> syscall_dispatch -> revoke_poll/info/ack 或原 resource API
  release/exit -> resource reclaim -> revoke_cancel
```

生产新代码最大模块 `revoke.c` 约 150 行；无堆分配、复杂控制宏或单体初始化文件。
旧异常/系统调用汇编继续服务原路径，异步 IRQ 专门保存段状态，不向原 60 字节 CPU 帧插入新字段。
