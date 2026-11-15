# Phase 2 实际验收记录

日期：2026-11-15（Asia/Taipei）。所有“通过”均来自当前云机器的实际执行。
没有将未执行的真实硬件、用户域或 LibOS 测试记为通过。

## 环境与复现命令

| 项目 | 实际配置 |
| --- | --- |
| 宿主 | Debian GNU/Linux 13，x86_64，普通用户 |
| 编译器/链接器 | GCC 14.2.0；GNU Binutils 2.44，ELF32/i386 |
| Make | GNU Make 4.4.1 |
| QEMU | 10.0.13，TCG，qemu32，32 MiB，单 CPU |
| GDB | 16.3，通过回环地址连接 QEMU stub |
| 宿主内存测试 | 同一份解析/页追踪源码，AddressSanitizer + UBSan |

沿用 Phase 1 已在仓库外安装的工具，不需要重新安装，也没有增加第三方库。
在仓库根目录：

```sh
export PATH=/workspace/.liberums-tools/bin:$PATH
make clean
make -j2
make test
make
git diff --check
```

最终执行顺序为：删除旧 build → 完整构建 → 完整测试 → 增量构建 → 差异检查。
最终完整构建、测试均返回 0；再次 make 报告无需重建。
最终轮测试期间没有修改源码。构建与测试完整输出保留在 `build/build.log` 和 `build/test.log`。
这些生成日志不加入 Git，`make clean` 会删除；本文保留关键证据。

## 逐阶段验证

2.1 完成时，先用独立测试构建执行 `ud2`，真实观察到向量 6、零错误码和 HALTED。
该临时编译开关随后移除，最终统一使用显式启动诊断参数。
2.2/2.3 完成时，直接编译解析与页追踪模块进行 ASan/UBSan 宿主测试；
随后 2.4/2.5 在 QEMU/GDB 中验证实际分页、故障与集成启动。

最终完整测试结果：

| 项目 | 实际结果 |
| --- | --- |
| 完整清理重建 | 通过；`-Wall -Wextra -Werror -Wpedantic`，无未解析符号 |
| 原 Phase 1 | 通过；ELF/Multiboot 头、串口三行、汇编/C 入口、调用栈参数及继续启动，原断言未放宽 |
| 宿主内存测试 | 通过；10,890 条断言，ASan/UBSan 无报告 |
| 宿主页分配穷尽 | 通过；2,169 页无重复、无保留页；耗尽、释放、重用与双重释放拒绝均执行 |
| 正常分页启动 | 通过；GDB 检查 CR0.PG/WP/PE、CR3、全部 1,024 项 PDE 和 16,384 项 PTE |
| 内核与引导页 | 通过；代码/常量只读，内核 BSS/栈/页表永久保留；引导数据仍映射且不可分配 |
| QEMU 物理页测试 | 通过；真实地图上的固件/内核/启动保留区、耗尽、唯一性、双重释放拒绝及重用 |
| 实际 Multiboot 模块 | 通过；加载 10,400 字节 initrd，检查描述符、模块内容和字符串的保留与映射 |
| 差异空白检查 | 通过；`git diff --check` 返回 0 |

宿主案例还包括：变长扩展记录、截断头/体、零长度、64 位溢出、128 条容量界限、
乱序/相邻可用区、保留区重叠、未对齐区域、超过 64 MiB 的裁剪和超过 4 GiB 的区域。

## 正常启动证据

默认 32 MiB 配置的串口：

```text
IDT: 32 exception gates ready
Memory map: accepted regions=0x00000006
Physical pages: free=0x00001ebc
Paging: enabled with WP, cr0=0x80010011
Phase 2: protected memory foundations ready
Liberums: boot successful
Phase 1: x86 32-bit C11 kernel, COM1 ready
Multiboot handoff accepted; interrupts disabled; halting.
```

GDB 在 `kernel_ready` 第一条指令处读取到：

```text
CR0 = 0x80010011   # PG=1, WP=1, PE=1
CR3 = 0x0011a000   # 4 KiB 对齐的实际页目录
EIP = 0x00100060
ESP = 0x00122fbc
```

测试读取真实页表并核对物理目标与权限，检查页零 absent、64 MiB 之外 PDE 全零、
所有 PDE/PTE 均不向用户开放。地址是本次构建值，不是稳定 ABI。

## 七个独立故障 guest

每个 case 都使用全新的 QEMU，通过 GDB 等待明确的 `kernel_stop` 路径。
检查串口中 CPU 现场的 EIP 与汇编故障标签相同，#PF 时还将报告 CR2 与 GDB 硬件 CR2 对照。
正常启动的 READY/boot successful 不允许出现在这些 guest 中。

| 参数 `selftest=` | 故障与实际向量 | 错误码 | 额外证据 |
| --- | --- | --- | --- |
| ud | 未定义指令，6 | 0（入口补齐） | EIP 为 `fault_ud` |
| divide | 除零，0 | 0（入口补齐） | 保存的 EAX 为 `0x13579bdf` |
| gp | 非法段选择子，13 | `0x18`（CPU 压入） | EIP 为段寄存器加载指令 |
| pf-read | 读取 64 MiB 边界，14 | 0（CPU 压入） | CR2=`0x04000000`，not-present read supervisor |
| pf-null | 读取页零，14 | 0（CPU 压入） | CR2=0 |
| ro-write | 写只读内核页，14 | 3（CPU 压入） | CR2=`0x00100000`，protection write supervisor |
| tlb | 预热后撤销页并重新读取，14 | 0（CPU 压入） | CR2=`0x00124000`，与该轮分配地址一致 |

七项全部通过。所有故障均输出 `PANIC: unrecoverable CPU exception` 和 `HALTED`。
`tlb` 不是直接读取原本不存在的页面：先真实写入并读回，再清 PTE、执行 `invlpg`，最后才触发访问。

## 十二个损坏输入 guest

GDB 在 `_start` 停下，修改 guest 的引导数据，不改代码，再继续真实启动路径：

1. 错误的 EAX magic。
2. 越界的 EBX 信息地址。
3. 空内存图。
4. 模块计数 `0xffffffff`。
5. 模块表指针越界。
6. 命令行 128 字节内没有 NUL。
7. 缺少内存图标志。
8. 包含尚未支持的可选数据标志。
9. 只有 3 字节的内存图。
10. 地图指针 `0xfffffff0`。
11. 单条记录 size=`0xffffffff`。
12. 记录的 64 位 begin+length 溢出。

十二项全部通过：报告 `invalid or unsupported Multiboot memory information` 并停止。
GDB 同时确认 PG 尚未开启、物理页追踪尚未初始化；没有异常崩溃，也没有继续进入 READY。

Phase 2 合计 **22 个独立 guest**：3 个正常/页状态/模块测试、7 个故障测试、12 个拒绝测试。
原 Phase 1 另有 2 个启动/调试 guest。没有跳过必需测试。

## 开发中发现的问题

- 扩展模块内容测试时，测试端最初把每段输入的长度算错（实际 26 字节），导致预期总长度错误。
  该轮确实失败；改为读取实际输入文件长度，并核对实际内容后通过。没有放宽内核保护断言。
- 一轮测试与新增启动日志的编辑重叠，旧符号地址与重新生成的 ELF 不一致，故障 EIP 比较失败。
  该轮不作为验收依据；最终改为固定源码、串行清理重建和完整测试，全部通过。
- 最终轮没有未解释的失败、未执行的必需项或环境阻塞。

## 日志与停止行为

- `build/boot-test.log`、`build/gdb-test.log`：原兼容性验收。
- `build/paging-*`、`build/pages-*`、`build/module-*`：正常及页状态/模块检查。
- `build/fault-*-serial.log`、`build/fault-*-gdb.log`：真实故障的串口与硬件检查。
- `build/reject-*-serial.log`、`build/reject-*-gdb.log`：损坏输入拒绝。

测试在取得证据后终止自己创建的进程组，所以日志尾部可能出现 QEMU 收到 SIGTERM、
嵌套 make 报 Terminated。这是测试清理，不是将非零启动退出或超时当作通过；
guest 意外退出、GDB 非零、断言失败和等待超时都会使总测试失败。

## 已知限制与未实现内容

- 只追踪/映射低 64 MiB；最多 128 条区域、8 个模块、128 字节字符串，超限明确失败。
- 可选符号/驱动/APM/VBE/framebuffer 数据暂不支持；不能声称任意 GRUB 配置均可启动。
- 分页前物理指针仍依赖可信加载器；范围检查不等于防御恶意固件或恶意加载器。
- 单 ring 0、单 CPU、无 IRQ/定时器/调度；没有 ring 3、TSS 特权级栈或异常恢复。
- 无独立 double-fault 栈或任务门；栈损坏时可能无法完成异常报告。
- 没有 NX/完整 W^X，没有 guard stack；内核可写页与页表仍属于可信 ring 0。
- 早期取页不是所有权能力：无域身份、授权句柄、代数、转移清零或可见撤销，identity mapping 仍覆盖 RAM。
- 没有 LibOS、文件系统、网络、通用设备透传；没有执行真实硬件测试。

Phase 2 到此停止。下一阶段建议从 TSS/特权级栈、ring 3 入口和独立用户地址空间开始，
先验证两个最小用户域之间的禁止访问，再规划资源所有权与 secure bindings。
