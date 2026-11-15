# Liberums — Exokernel Experiment

一个从零编写的教学与实验操作系统，目标是逐步实现资源保护与管理分离、
可验证的资源所有权和用户态 LibOS。语言为 C11 与少量 x86 汇编，
目标为 **32 位 x86（i686）+ QEMU**，许可证为 **GPL-3.0-or-later**。

**当前完成 Phase 3：两个真实 Ring 3 用户域、独立地址空间、受控系统调用和用户故障隔离。
尚无时钟抢占、公平 CPU 分配、secure bindings、可见撤销或 LibOS，不能将它视为完整 Exokernel。**
实现代码为本项目编写；引导遵循公开的 Multiboot v1 协议。

## 目录结构

```text
Liberums/
├── arch/x86/
│   ├── boot.S          # Multiboot 头、GDT、BSS、栈及 C 入口调用
│   ├── linker.ld       # ELF32 布局与页对齐只读区
│   ├── idt.c / isr.S   # CPU 异常现场与诊断
│   ├── paging.c        # supervisor identity mapping、PG/WP、TLB
│   ├── test_faults.S   # Ring 0 故障诊断
│   └── gdt.c / user_context.S / user_images.S # TSS、iret 与内置用户映像
├── include/
│   ├── arch/x86/io.h   # x86 端口 I/O 原语
│   └── kernel/        # 串口、内存图、页追踪、引导数据、panic 等接口
├── kernel/
│   ├── main.c         # 初始化编排与运行时检查
│   ├── serial.c       # COM1，115200 baud / 8N1，轮询发送
│   ├── boot_info.c / memory_map.c # 有界引导信息与地图解析
│   ├── physical_pages.c # 页状态与早期取页/归还
│   ├── panic.c / selftest.c # 停止处理与显式诊断
│   └── domain.c / syscall.c # 域生命周期与三个受控调用
├── tests/
│   ├── entry.gdb      # 汇编/C 入口及机器状态断言
│   ├── phase1.py      # 原 ELF、启动、GDB 兼容验收
│   ├── memory_tests.c # 宿主 ASan/UBSan 边界与状态测试
│   ├── phase2.py / paging.gdb # 原分页/异常/坏输入验收
│   └── phase3.py      # Ring 3、双向隔离、TSS/CR3 与回收
├── user/              # 两个独立链接的 C 测试映像、寄存器/故障汇编
├── docs/
│   ├── architecture.md # 内核/LibOS 边界与后续资源保护设计
│   ├── phase1-validation.md # 历史验收记录
│   ├── phase2-*.md    # 概览、模块、内存原理与历史验收
│   └── phase3-*.md    # 用户域、特权级、地址空间与实际验收
├── Makefile
├── LICENSE
└── build/             # 生成的 ELF、对象、链接映射、测试日志（Git 忽略）
```

用户测试映像位于 `user/`；未来 LibOS 放入 `libos/`，本阶段没有实现 LibOS。

## 工具要求

- GNU Make、支持 `-m32` 的 GCC、支持 `elf_i386` 的 GNU Binutils（含 `nm`）。
- `qemu-system-i386`，使用 TCG 软件模拟，不依赖 KVM 或宿主机 root 权限。
- 支持 i386 调试及 Python 的 GDB；Python 3 用于自动验收。
- 宿主 GCC 和 AddressSanitizer/UBSan 运行库，用于测试真实的地图解析与页追踪 C 模块。

Debian/Ubuntu 常规系统可安装：

```sh
sudo apt-get update
sudo apt-get install build-essential binutils qemu-system-x86 gdb python3
```

内核采用 freestanding 编译及直接链接，不链接宿主 libc，也不需要 32 位用户态 libc。
如果现有 GCC 不支持 i386，可使用已有的交叉工具链：

```sh
make CROSS_COMPILE=i686-elf-
```

当前云环境的 QEMU/GDB 位于仓库外，先执行下面一行即可使用本文所有命令：

```sh
export PATH=/workspace/.liberums-tools/bin:$PATH
```

这一路径只适用于本次已准备的云环境；其他机器按上面的常规方式安装工具。

## 构建与运行

在仓库根目录执行：

```sh
make                # 生成 build/liberums.elf，保留 GDB 调试符号
make run            # 实际启动 QEMU，COM1 映射到当前终端
make debug          # 启动后暂停 CPU，GDB 仅监听 127.0.0.1:1234
make clean          # 删除 build/，不删除源码或外部工具
```

也可指定工具路径，例如 `make run QEMU=/path/to/qemu-system-i386`。
`make debug GDB_PORT=1235` 可换用空闲端口。

正常串口会先输出 IDT、内存图、可用页数和 CR0 的阶段信息，然后输出：

```text
Phase 2: protected memory foundations ready
Liberums: boot successful
Phase 1: x86 32-bit C11 kernel, COM1 ready
Multiboot handoff accepted; interrupts disabled; halting.
```

其中三行 Phase 1 文本保留为兼容性输出，不表示分页仍然关闭。
随后进入用户域阶段，正常输出如下；每次启动创建、运行和回收两轮：

```text
ABab
Domains: round completed, all pages reclaimed
ABab
Domains: round completed, all pages reclaimed
Phase 3: user domains completed
```

原三行兼容信息中的 `halting` 是历史文本；最终停止发生在用户域完成后。
最终内核停留在 `cli; hlt`，没有 shell。
前台运行时按 **Ctrl+C** 退出 QEMU。`make debug` 在连接 GDB 并继续运行之前不会输出。

## 引导路径

1. `QEMU -kernel` 使用内置 Multiboot 加载器识别 ELF 前 8 KiB 中的头部。
   本阶段无自写实模式磁盘引导器，也不生成 ISO 或裸磁盘镜像。
2. 加载器将各 ELF LOAD 段加载到物理地址，切换到 32 位保护模式、关闭分页，
   通过 EAX 传递 `0x2badb002`、EBX 传递 Multiboot 信息结构地址，跳到 `_start`。
3. `_start` 禁用中断、清方向标志，自建平坦 ring 0 GDT 并重新加载段寄存器，
   清零 BSS，建立 16 KiB、16 字节对齐的内核栈，按 i386 C 调用约定进入 `kernel_main`。
4. C 入口依次初始化 COM1 和 IDT，验证并解析 Multiboot 内存图，保留内核与引导数据，
   建立物理页状态和初始页表，设置 CR0.PG/WP，再检查状态、输出信息并停机。

没有依赖加载器遗留的栈或 GDT。当前仍保持外部可屏蔽中断关闭，异常经 IDT 报告后停止，
Ring 0 异常无法恢复时继续 Panic；没有独立紧急故障栈。内核映射最多低 64 MiB 的 RAM 和必要启动数据，页零不映射。
实际 PTE 与 CR0.WP 保护内核代码和只读数据；普通 32 位分页没有 NX，不能宣称完整 W^X。
ELF 属性自身仍不等于硬件页面权限。
5. Phase 2 检查之后加载扩展 GDT/TSS，为每域创建独立 CR3、用户代码/数据/栈，
   通过 iret 进入 Ring 3；系统调用或用户异常用 TSS 切回可信内核栈。
   只有三个标量调用，无用户指针接口；按固定顺序合作式切换，没有抢占。

## GDB 入口验证

终端一执行 `make debug`，终端二在仓库根目录执行：

```sh
gdb -q -nx build/liberums.elf
```

```gdb
set architecture i386
target remote 127.0.0.1:1234
hbreak *_start
continue
info registers eip eax ebx
hbreak *kernel_main
continue
info registers eip esp cs ds ss cr0 eflags
x/3wx $esp
detach
quit
```

在 `_start` 应看到 EAX=`0x2badb002`、EBX 非零。在 `kernel_main` 第一条指令处，
栈中的返回地址后应是同一组 magic 与信息地址，`ESP % 16 == 12`。
`detach` 后 QEMU 继续输出启动信息，再在终端一退出 QEMU。

自动执行相同的入口验证与串口验收：

```sh
make test                 # 宿主测试 + Phase 1/2 回归 + 全部 Phase 3 验收
make test-memory          # 可单独运行宿主内存测试
# 可单独执行某项（需先 make）：
python3 tests/phase1.py --mode boot
python3 tests/phase1.py --mode gdb
python3 tests/phase2.py
python3 tests/phase3.py
```

Phase 1 兼容性测试仍检查 ELF 类型、Multiboot 头位置与校验和、链接符号，实际调用 `make run`
核对三行串口输出，再调用 `make debug`，使用硬件断点命中 `_start` 和 `kernel_main`，
验证保护模式、关闭分页、中断/方向标志、段寄存器、栈及 C 参数，最后验证继续启动。
GDB 使用临时回环端口。缺少工具、超时或断言失败均返回非零，不会记作通过或跳过。
测试结束只终止自己启动的进程组。

日志为 `build/boot-test.log`、`build/gdb-test.log` 和 `build/debug-serial.log`；
`make clean` 会删除它们。Phase 2 的独立 guest 还生成 `build/paging-*`、`build/fault-*`、
`build/reject-*` 等日志。完整结果见 [Phase 2 验收记录](docs/phase2-validation.md)，
旧结果保留在 [Phase 1 验收记录](docs/phase1-validation.md)。

入门阅读顺序：[Phase 2 概览](docs/phase2-overview.md) → [模块说明](docs/phase2-modules.md)
→ [内存原理](docs/phase2-memory.md)。手动故障测试可执行
`make run QEMU_APPEND=selftest=ro-write`，观察 #PF 后停止；普通 `make run` 不执行破坏性测试。

Phase 3 教学文档：[概览](docs/phase3-overview.md)、[模块](docs/phase3-modules.md)、
[特权级与调用 ABI](docs/phase3-privilege.md)、[地址空间](docs/phase3-address-spaces.md)、
[实际验收](docs/phase3-validation.md)。日志为 `build/phase3-*-serial.log` 与 `*-gdb.log`。

安全实验：`make run QEMU_APPEND=domain-test=cross-read` 会让 A 故障、B 正常结束；
追加 `domain-reverse` 可反向测试。普通启动不执行破坏性测试。

## Exokernel 边界与下一步

内核未来负责保护机制：资源身份、所有权、授权检查、受保护映射、异常隔离、
最低限度的 CPU 分配及可见撤销。LibOS 负责管理策略：虚拟地址布局、堆分配、
文件系统、缓存、网络协议、用户线程调度与应用接口。
早期 COM1 仅为可信内核诊断通道，不是把通用设备管理策略放进内核的先例。

完整设计与下一阶段建议见 [架构规划](docs/architecture.md)。
Phase 3 到此结束。当前六页列表只是内核内部的生命周期记录，不能等同于可委托的资源权利。
下一阶段再设计 Resource Ownership 与 Secure Bindings；当前没有提供用户资源分配/映射接口。
合作式执行下恶意用户可无限占用 CPU，没有公平性或强制 CPU 撤销保证。

## 许可证

项目代码与文档按 **GNU General Public License v3.0 or later** 发布，
SPDX 标识为 `GPL-3.0-or-later`，完整条款见 [LICENSE](LICENSE)。
