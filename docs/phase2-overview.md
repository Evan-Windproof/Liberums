# Phase 2 — Protected Memory Foundations

本阶段在 Phase 1 的引导、GDT、栈和串口基础上，加入异常处理、内存图解析、
物理页追踪和基础分页。启动汇编没有被重写。
当前依然只有一个可信 ring 0 内核：没有 LibOS、ring 3 用户域、文件系统或调度器。

## 为什么先做这些

C 的指针只是地址。没有 CPU 的保护机制时，错误地址可能覆盖别的数据，
而程序甚至不知道发生了什么。分页让 CPU 检查地址是否存在、是否允许写入；
异常处理让我们看到故障发生在哪里，而不是只观察到机器重启。

同样，QEMU 配置了 32 MiB，并不表示从物理地址 0 到 32 MiB 的每一个字节都能分配。
固件、内核、页表、栈和引导信息都需要保留。内存图提供原始区域信息，页状态表再把
这些字节范围转为 4 KiB 页的状态。

## 从启动到完成

```text
Multiboot loader
       |
_start：原有 GDT、BSS 清零、16 KiB 栈
       |
kernel_main（仍在 ring 0）
       +--> serial_init：尽早让错误可见
       +--> idt_init：安装 0..31 异常入口
       +--> boot_info_read
       |       +--> memory_map_parse：检查每条记录
       |       +--> 保留信息结构、内存图、字符串和模块
       +--> physical_pages_init：可用页与永久保留区
       +--> paging_init：页目录、页表、CR3、CR0.PG/WP
       +--> 检查 CR0 和可用页数量
       +--> selftest_run：仅显式 selftest=... 时执行诊断
       +--> kernel_ready：GDB 运行时检查点
       +--> Phase 2 信息、原 Phase 1 三行兼容输出
       +--> kernel_stop：cli/hlt
```

顺序遵循任务要求。先装异常入口，后启用分页，才能在初始分页出错时尽可能报告 #PF。
所有页表、异常代码、IDT、栈和状态表都静态放在内核 ELF 范围内，先永久保留，再启用分页。
引导数据在切换前完成验证，其页面也保留 identity mapping，所以切换后仍能读取。

## 正常运行与破坏性测试

普通 `make run` 不主动制造异常，成功后停机。`make test` 会先运行宿主内存测试，
再运行原 Phase 1 测试，最后为每种异常和损坏输入启动独立的 QEMU guest。
测试不会把“输出了启动字符串”当作所有底层机制有效的证据；还检查 GDB 寄存器、
实际页表、异常 EIP、CPU 错误码与 CR2。参见 [验收记录](phase2-validation.md)。

手动实验：

```sh
make run QEMU_APPEND=selftest=pages     # 耗尽/归还物理页，不破坏正常启动
make run QEMU_APPEND=selftest=ud        # 故意执行未定义指令，报告后停止
make run QEMU_APPEND=selftest=ro-write  # 故意写只读内核页，产生 #PF
```

参数由内核按完整空格分隔 token 匹配，避免把 `selftest=ud-extra` 当作 `selftest=ud`。
诊断选项不是用户态系统调用或稳定 ABI。错误选项不会变成新的资源管理策略。

## 与 Exokernel 的关系

本阶段提供未来资源保护的基础，而不是完整资源隔离：

- 页状态表回答“这页现在是否可供内核早期使用”，还没有回答“哪个用户域持有何种权利”。
- 页表给内核代码/常量提供只读保护，但没有创建彼此隔离的用户地址空间。
- 异常入口记录故障并停止，不会恢复用户域或调度另一个任务。

未来需要在这些机制之上加入域身份、授权句柄、所有权转移和可见撤销。
堆分配策略、文件系统布局、用户线程策略仍应由 LibOS 选择。

下一阶段建议先设计 TSS/特权级栈与 ring 3 陷阱路径，再建立两个最小用户域，
验证越权映射和跨域读写被拒绝。本阶段没有实现这些内容。
