# Liberums — Exokernel Experiment

一个从零编写的教学与实验操作系统，目标是逐步实现资源保护与管理分离、
可验证的资源所有权和用户态 LibOS。语言为 C11 与少量 x86 汇编，
目标为 **32 位 x86（i686）+ QEMU**，许可证为 **GPL-3.0-or-later**。

**当前完成 Phase 8：有界动态四域、多个独立 ELF32、IRQ0 轮转、资源隔离与安全复用。**
公开 ABI 1.2 保持；Phase 7.1～7.3 的用户态 VGA/键盘 Console 与独占设备授权继续保留。
动态域采用独立 CR3/栈，复用前回收设备、Capability、撤销事件和 RAM，推进 Epoch。
见 [Phase 8 概览](docs/phase8-overview.md)、[教学模块图](docs/phase8-modules.md) 和
[实际 QEMU/GDB 验收](docs/phase8-validation.md)。
默认保留 Phase 1～5 的合作兼容模式；显式 `preempt` 开放定时器，非合作用户不能阻止其他 READY 域运行。
撤销先通知并提供 ACK 机会，超时由内核移除全部别名并清零回收。仍是单 CPU 教学系统，没有完整 LibOS。
实现代码为本项目编写；引导遵循公开的 Multiboot v1 协议。

## 目录结构

```text
Liberums/
├── arch/x86/
│   ├── boot.S          # Multiboot 头、GDT、BSS、栈及 C 入口调用
│   ├── linker.ld       # ELF32 布局与页对齐只读区
│   ├── idt.c / isr.S   # CPU 异常现场与诊断
│   ├── paging.c        # supervisor identity mapping、PG/WP、TLB
│   ├── pit.c / pic.c / irq.S # IRQ0 时钟、控制器、独立异步现场
│   ├── test_faults.S   # Ring 0 故障诊断
│   ├── gdt.c / user_context.S / user_images.S # TSS、iret 与内置用户映像
│   └── user_space.c   # 外部映像/栈的独立多页构造与回滚
├── include/
│   ├── abi/           # 独立 SDK：版本、调用、资源、ELF profile
│   ├── arch/x86/io.h   # x86 端口 I/O 原语
│   └── kernel/        # 串口、内存图、页追踪、引导数据、panic 等接口
├── kernel/
│   ├── main.c         # 初始化编排与运行时检查
│   ├── serial.c       # COM1，115200 baud / 8N1，轮询发送
│   ├── boot_info.c / memory_map.c # 有界引导信息与地图解析
│   ├── physical_pages.c # 页状态与早期取页/归还
│   ├── panic.c / selftest.c # 停止处理与显式诊断
│   ├── domain.c / syscall.c # 域生命周期与标量系统调用
│   ├── domain_pool.c / multi_external.c # 有界动态槽位、多模块安全创建与回收
│   ├── resource.c / capability.c # 所有权、绑定生命周期、代数与权限
│   ├── timer.c / tick.c / scheduler.c # 单调 tick 与最小轮转
│   ├── revoke.c       # 有界事件、期限、ACK 与强制回收
│   └── elf_loader.c / external.c # 有界 ELF 校验与外部执行编排
├── tests/
│   ├── entry.gdb      # 汇编/C 入口及机器状态断言
│   ├── phase1.py      # 原 ELF、启动、GDB 兼容验收
│   ├── memory_tests.c # 宿主 ASan/UBSan 边界与状态测试
│   ├── phase2.py / paging.gdb # 原分页/异常/坏输入验收
│   ├── phase3.py      # Ring 3、双向隔离、TSS/CR3 与回收
│   ├── phase4.py / capability_tests.c # 资源/转移硬件验收与宿主边界检查
│   ├── phase6.py / timer_tests.c / revoke_tests.c # 真实抢占/撤销及边界验收
│   └── phase5.py / fixtures/external_user/ # 外部 ABI/ELF 集成
├── user/              # 原 A/B 映像及分组的资源测试映像，每个仍限制一页
├── docs/
│   ├── architecture.md # 内核/LibOS 边界与后续资源保护设计
│   ├── phase1-validation.md # 历史验收记录
│   ├── phase2-*.md    # 概览、模块、内存原理与历史验收
│   ├── phase3-*.md    # 用户域、特权级、地址空间与历史验收
│   ├── phase4-*.md    # 所有权、绑定、接口、模块与历史验收
│   ├── phase6-*.md    # 时钟、抢占、撤销、模块与实际验收
│   └── phase5-*.md    # 公开 ABI、ELF 装载、模块与实际验收
├── Makefile
├── LICENSE
└── build/             # 生成的 ELF、对象、链接映射、测试日志（Git 忽略）
```

内置测试映像位于 `user/`。Aelthuvor 是独立仓库；Liberums 不包含其源码，也不链接其实现。测试仅通过外部 ELF 对接。

新增 `make run-multi` 可同时装载两个独立测试 ELF；通过逗号分隔的 `MULTI_ELFS` 可指定最多八个
Multiboot 模块，其中最多四域同时建立。默认无设备权限且无窗口；旧 run 系列行为保留。
`make test-phase8` 单独执行动态域验收，`make test` 包含全部历史与本阶段回归。
公开每域资源配额仍为四页，没有新增用户 spawn、fork/exec 或应用管理服务。

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

没有依赖加载器遗留的栈或 GDT。默认兼容模式保持外部可屏蔽中断关闭，
Ring 0 异常无法恢复时继续 Panic；没有独立紧急故障栈。内核映射最多低 64 MiB 的 RAM 和必要启动数据，页零不映射。
实际 PTE 与 CR0.WP 保护内核代码和只读数据；普通 32 位分页没有 NX，不能宣称完整 W^X。
ELF 属性自身仍不等于硬件页面权限。
5. Phase 2 检查之后加载扩展 GDT/TSS，为每域创建独立 CR3、用户代码/数据/栈，
   通过 iret 进入 Ring 3；系统调用或用户异常用 TSS 切回可信内核栈。
   原调用和资源标量接口继续保留；默认按原顺序合作式切换，显式抢占模式由 IRQ0 强制轮转。

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
make test                 # 宿主测试 + Phase 1～6 全部验收
make test-memory          # 可单独运行宿主内存及 capability 测试
# 可单独执行某项（需先 make）：
python3 tests/phase1.py --mode boot
python3 tests/phase1.py --mode gdb
python3 tests/phase2.py
python3 tests/phase3.py
python3 tests/phase4.py
python3 tests/phase5.py
python3 tests/phase6.py      # 含宿主 tick/撤销边界与 QEMU/GDB 验收
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

Phase 4 入门：[概览](docs/phase4-overview.md)、[资源所有权](docs/phase4-resource-ownership.md)、
[安全绑定与完整 ABI](docs/phase4-secure-bindings.md)、[模块](docs/phase4-modules.md)、
[实际验收](docs/phase4-validation.md)。

每域在原六页基础外最多取得 4 页动态资源，可选择 PFN，并映射到
`[0x09000000, 0x09010000)` 中的页对齐 VA。句柄为 index + 完整 32 位 generation，
授权再核对可信域 ID/epoch/rights；知道另一个域的句柄或 PFN 不会获得权限。
release 撤销全部别名并清零；独占转移先撤销旧域映射、同步 TLB、清零，再交付新授权。

```sh
python3 tests/phase4.py --mode basic       # 含已知敌对句柄的注入，不改所有权/PTE
python3 tests/phase4.py --mode transfer    # 权限缩减、失败回滚、全别名撤销及零泄漏
make run QEMU_APPEND=resource-test=unmap-fault
```

basic/transfer 的完整攻击验证和 generation 边界验证需要脚本的 GDB 测试输入。
普通 `make run` 继续原正常行为，不自动执行破坏性测试。日志为 `build/phase4-*-serial.log`
和 `*-gdb.log`，完整测试输出为 `build/test.log`，生成物不提交。

## 外部 ELF32 与公开 ABI

```sh
make external-fixtures
make run QEMU_APPEND=external QEMU_INITRD=build/external/normal.elf
make debug QEMU_APPEND=external QEMU_INITRD=build/external/normal.elf
python3 tests/phase5.py
```

只有显式 external token 才加载恰好一个模块；默认 make run 继续原正常流程。
独立程序只需 `include/abi/`：ABI 1.2（兼容默认模式下的 1.0/1.1）、版本查询号 3、额度查询号 4、Feature Flags，以及保留原调用号的资源 API。
外部程序可有多个代码/只读/数据/BSS 页和四页零栈；旧单页断言与六页路径继续独立验证。

教学文档：[概览](docs/phase5-overview.md)、[公开 ABI/接入规范](docs/phase5-abi.md)、
[ELF profile](docs/phase5-elf-loader.md)、[模块](docs/phase5-modules.md)、
[实际验收](docs/phase5-validation.md)。没有动态链接器或文件系统；Aelthuvor 实现位于独立仓库。

## 独立 Aelthuvor Phase 1 集成

将 [Aelthuvor](https://github.com/Evan-Windproof/Aelthuvor) 克隆为同级目录（也可覆盖路径）：

```sh
make -C ../Aelthuvor
make test-libos                     # 默认 AELTHUVOR=../Aelthuvor
make test-libos AELTHUVOR=/path/to/Aelthuvor
make run QEMU_APPEND=external QEMU_INITRD=../Aelthuvor/build/aelthuvor.elf
```

`make test` 可在只有内核仓库时运行原 103 个及新增 10 个 QEMU 场景。
以下 `make test-libos` 是历史跨仓库工作流，会构建另一个仓库；本次 Phase 6 没有运行它。
旧集成脚本中的精确版本/SDK 相等检查需要未来在独立项目升级时审查；运行旧 ELF 不需要更换 SDK。
历史 `make test-libos`
另构建独立 LibOS，运行 9 个 QEMU/GDB 场景、各两轮，检查用户分配策略、真实资源绑定、
正常退出/故障回收、ABI 拒绝及部分加载回滚。源码和依赖互不混入。
日志在各自 `build/` 下，详见 [双仓库验收](docs/phase5-validation.md#双仓库集成追加验收)。

## Exokernel 边界与下一步

内核负责保护机制：资源身份、所有权、授权检查、受保护映射、异常隔离、
最低限度的 CPU 分配及可见撤销。LibOS 负责管理策略：虚拟地址布局、堆分配、
文件系统、缓存、网络协议、用户线程调度与应用接口。
早期 COM1 仅为可信内核诊断通道，不是把通用设备管理策略放进内核的先例。

完整设计与下一阶段建议见 [架构规划](docs/architecture.md)。
Phase 6 到此结束，不自动进入下一阶段，不修改 Aelthuvor。
默认模式继续支持旧 SDK ELF，本次使用来源/哈希固定的 ABI 1.1 兼容映像，不把它声称为真实 Aelthuvor 产物。
现有 Aelthuvor 默认仍应选择 `QEMU_APPEND=external`；新撤销协议应在其独立升级任务中明确接入。

## Phase 6 抢占与可见撤销实验

```sh
make run QEMU_APPEND=phase6=spin
make run QEMU_APPEND=phase6=ack
make run QEMU_APPEND=phase6=force
make run QEMU_APPEND=preempt
make external-fixtures
make run QEMU_APPEND='external preempt' QEMU_INITRD=build/external/preempt.elf
make debug QEMU_APPEND=phase6=force
make test-phase6
```

默认 flags=31、用户 IF=0；抢占模式 flags=119、用户 IF=1。COOPERATIVE 与 PREEMPTION 不同时声明。
新号 24/25/26 提供 POLL/INFO/ACK，只有标量，没有用户指针。每域两个事件，期限按实际 IRQ tick 计数。
普通资源数据页支持全别名强制回收；不撤销内核栈、活动页表或 DMA。没有硬实时保证或自动内存压力策略。
详细调用关系、教学说明及测试证据见 [概览](docs/phase6-overview.md)、[模块](docs/phase6-modules.md)、
[抢占](docs/phase6-preemption.md)、[撤销 ABI](docs/phase6-revocation.md)、[实际验收](docs/phase6-validation.md)。

## Phase 7.1：VGA 用户态 Console

显式 `make run-vga` 在支持 GTK 的本地 QEMU 中启动 Ring 3 文本 Console；
`make run-vga-diag` 只执行独立 Ring 0 诊断。默认 `make run` 和 COM1 保持不变。
可覆盖 `QEMU_DISPLAY=none`；云端通过实际显存、PTE、Ring 3 和 QMP 截图验收，
没有验证本地 WSLg 窗口。`make test-vga` 执行新设备边界与硬件测试，`make test` 包含新旧回归。
接口是 ABI 1.2 的可探测可选扩展，旧 ELF 保持兼容。
机制、页粒度限制、运行命令及证据见 [Phase 7.1 教学文档](docs/phase7-vga-console.md)。

## Phase 7.2：Keyboard Input 与交互 Console

`make run-console` 启动 PS/2 IRQ1 + Ring 3 交互 Console（默认 GTK，可覆盖 `QEMU_DISPLAY`）。
支持回显、Backspace、Enter、光标及 `help` / `clear` / `about` / `status`。
内核只提供有界原始扫描码队列、授权和必要端口访问，字符解释/输入编辑/命令均在用户态。
原 `make run`、`make run-vga`、COM1、IRQ0 和 ABI 1.2 保持原有行为。
`make test-keyboard` 执行新 native/QMP/GDB 验收，完整 `make test` 包含旧新阶段。
详见 [Phase 7.2 教学文档与证据](docs/phase7-keyboard-console.md)。

## Phase 7.3：独立 ELF Console

```sh
make -j2
make external-console
make run-external-console
make run-external-console CONSOLE_ELF=build/external/amber.elf
```

新路径使用普通外部 ELF、公开 ABI 与显式可信设备 profile；ELF Note/文件名不授予权限。
Console 策略保留在共享 Ring 3 模块，内核不解释命令或颜色。
`run-external-console` 使用已构建内核，允许仅重建/替换用户 ELF；内核缺失时先执行 `make`。
原 `run` / `run-vga` / `run-console` 保留，`make test` 包含新旧回归。
见 [Phase 7.3 概览](docs/phase73-overview.md)、[架构边界审查](docs/exokernel-boundary-review.md)。

## 许可证

项目代码与文档按 **GNU General Public License v3.0 or later** 发布，
SPDX 标识为 `GPL-3.0-or-later`，完整条款见 [LICENSE](LICENSE)。
