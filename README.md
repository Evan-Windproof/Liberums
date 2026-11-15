# Liberums — Exokernel Experiment

一个从零编写的教学与实验操作系统，目标是逐步实现资源保护与管理分离、
可验证的资源所有权和用户态 LibOS。语言为 C11 与少量 x86 汇编，
目标为 **32 位 x86（i686）+ QEMU**，许可证为 **GPL-3.0-or-later**。

**当前只完成 Phase 1：可启动的内核基础。尚无用户态、资源隔离、secure bindings、
撤销机制或 LibOS，不能将它视为已具备隔离能力的 Exokernel。**
实现代码为本项目编写；引导遵循公开的 Multiboot v1 协议。

## 目录结构

```text
Liberums/
├── arch/x86/
│   ├── boot.S          # Multiboot 头、GDT、BSS、栈及 C 入口调用
│   └── linker.ld       # ELF32 布局，加载基址 1 MiB
├── include/
│   ├── arch/x86/io.h   # x86 端口 I/O 原语
│   └── kernel/serial.h # 早期诊断输出接口
├── kernel/
│   ├── main.c         # 验证引导参数、输出成功信息、停机
│   └── serial.c       # COM1，115200 baud / 8N1，轮询发送
├── tests/
│   ├── entry.gdb      # 汇编/C 入口及机器状态断言
│   └── phase1.py      # ELF、实际启动、实际 GDB 自动验收
├── docs/
│   ├── architecture.md # 内核/LibOS 边界与后续资源保护设计
│   └── phase1-validation.md # 本次实际验收记录与限制
├── Makefile
├── LICENSE
└── build/             # 生成的 ELF、对象、链接映射、测试日志（Git 忽略）
```

未来的 LibOS 代码将放入 `libos/`，用户程序放入 `user/`；本阶段不创建空实现。

## 工具要求

- GNU Make、支持 `-m32` 的 GCC、支持 `elf_i386` 的 GNU Binutils（含 `nm`）。
- `qemu-system-i386`，使用 TCG 软件模拟，不依赖 KVM 或宿主机 root 权限。
- 支持 i386 调试及 Python 的 GDB；Python 3 用于自动验收。

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

正常串口输出为：

```text
Liberums: boot successful
Phase 1: x86 32-bit C11 kernel, COM1 ready
Multiboot handoff accepted; interrupts disabled; halting.
```

随后内核停留在 `cli; hlt` 循环，这是预期结果；没有 shell 或用户任务。
前台运行时按 **Ctrl+C** 退出 QEMU。`make debug` 在连接 GDB 并继续运行之前不会输出。

## 引导路径

1. `QEMU -kernel` 使用内置 Multiboot 加载器识别 ELF 前 8 KiB 中的头部。
   本阶段无自写实模式磁盘引导器，也不生成 ISO 或裸磁盘镜像。
2. 加载器将各 ELF LOAD 段加载到物理地址，切换到 32 位保护模式、关闭分页，
   通过 EAX 传递 `0x2badb002`、EBX 传递 Multiboot 信息结构地址，跳到 `_start`。
3. `_start` 禁用中断、清方向标志，自建平坦 ring 0 GDT 并重新加载段寄存器，
   清零 BSS，建立 16 KiB、16 字节对齐的内核栈，按 i386 C 调用约定进入 `kernel_main`。
4. C 入口初始化 COM1，检查 magic 与信息指针非空，输出诊断信息，然后停机。
   **尚未解析内存图，也未据此分配资源。**

没有依赖加载器遗留的栈或 GDT。当前没有 IDT，整个阶段保持可屏蔽中断关闭；
异常和 NMI 尚无处理能力。ELF 段具有独立 R/X、R、R/W 标志，
但分页关闭时这些标志不构成硬件内存保护。

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
make test
# 可单独执行某项（需先 make）：
python3 tests/phase1.py --mode boot
python3 tests/phase1.py --mode gdb
```

测试会检查 ELF 类型、Multiboot 头位置与校验和、链接符号，实际调用 `make run`
核对三行串口输出，再调用 `make debug`，使用硬件断点命中 `_start` 和 `kernel_main`，
验证保护模式、关闭分页、中断/方向标志、段寄存器、栈及 C 参数，最后验证继续启动。
GDB 使用临时回环端口。缺少工具、超时或断言失败均返回非零，不会记作通过或跳过。
测试结束只终止自己启动的进程组。

日志为 `build/boot-test.log`、`build/gdb-test.log` 和 `build/debug-serial.log`；
`make clean` 会删除它们。实际测试结果见 [Phase 1 验收记录](docs/phase1-validation.md)。

## Exokernel 边界与下一步

内核未来负责保护机制：资源身份、所有权、授权检查、受保护映射、异常隔离、
最低限度的 CPU 分配及可见撤销。LibOS 负责管理策略：虚拟地址布局、堆分配、
文件系统、缓存、网络协议、用户线程调度与应用接口。
早期 COM1 仅为可信内核诊断通道，不是把通用设备管理策略放进内核的先例。

完整设计与下一阶段建议见 [架构规划](docs/architecture.md)。
Phase 1 到此结束；资源隔离和用户态属于之后独立验收的工作。

## 许可证

项目代码与文档按 **GNU General Public License v3.0 or later** 发布，
SPDX 标识为 `GPL-3.0-or-later`，完整条款见 [LICENSE](LICENSE)。
