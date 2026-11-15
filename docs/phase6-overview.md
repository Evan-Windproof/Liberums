# Phase 6：可见撤销与 CPU 抢占

本阶段增加真实 PIT IRQ0、单 CPU 轮转仲裁和普通用户资源页的可见撤销。
前五阶段的启动、内存保护、Ring 3、capability 和外部 ELF 路径继续保留。
没有引入文件系统、POSIX、线程系统或新的 LibOS。

## 为什么原来的 yield 不够

`yield` 是用户自愿交还 CPU。用户如果执行 `for (;;) {}`，合作式内核无法继续运行 B，
也无法保证撤销期限得到处理。PIT 是独立于用户指令流的定时器；IRQ0 经 PIC 到达 CPU，
CPU 强制进入内核。用户不能执行 CLI，也不能改变内核的中断门或页表。

内核只负责收回执行权、轮转两个域和验证资源保护。LibOS 仍选择堆算法、缓存驱逐、
数据迁移和用户线程顺序。抢占使保护动作最终能够发生，不替 LibOS 决定资源的用途。

## 执行流程

```text
Multiboot -> 原 Phase 1/2 初始化 -> 读取显式启动选项
  默认：合作兼容模式，IF=0，原 A/B 或外部 ELF
  preempt / phase6=...：初始化 PIT/PIC（此时内核 IF 仍为 0）
    -> GDT/TSS、每域 CR3 -> iret 到用户，IF=1
    -> IRQ0 -> 保存完整用户现场 -> EOI -> 可信调度栈/内核 CR3
    -> 处理到期撤销 -> 清理终止域 -> 轮转下一个 READY 域
    -> 所有域终止 -> 销毁地址空间、核对空闲页 -> 屏蔽 PIC、停止
```

`main.c` 编排启动；`domain.c` 处理域身份和一次进入/离开；`scheduler.c` 负责轮转；
`timer.c` 计时；`revoke.c` 维护协议；`resource.c` 执行真正的绑定撤除和清零。

## 运行

```sh
make
make run                                      # 原兼容流程
make run QEMU_APPEND=preempt                   # 原 A/B，允许硬件抢占
make run QEMU_APPEND=phase6=spin                # A 无 yield，B 正常退出
make run QEMU_APPEND=phase6=ack                 # 主动确认撤销
make run QEMU_APPEND=phase6=force               # 不合作写循环与强制撤销
make run QEMU_APPEND=phase6=release             # 普通 release 取消事件
make run QEMU_APPEND=phase6=exit-pending        # 退出时清理事件
make external-fixtures
make run QEMU_APPEND='external preempt' QEMU_INITRD=build/external/preempt.elf
python3 tests/phase6.py
```

五个 `phase6=` 场景是显式诊断，不是自动内存压力策略。诊断域每轮最多 20 次抢占，
超出后以 EBUSY 状态终止，便于实验结束；普通 `preempt` 没有这个执行预算。
生产路径提供可信 `revoke_request()` 接口，本阶段没有实现自动压力驱逐选择。
详见[中断](phase6-interrupts.md)、[抢占](phase6-preemption.md)、[撤销](phase6-revocation.md)
和[实际验收](phase6-validation.md)。
