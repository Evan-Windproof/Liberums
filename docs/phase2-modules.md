# Phase 2 模块说明

所有跨模块 C 接口都在 `include/` 中声明；文件内部辅助函数使用 `static`。
架构相关代码放在 `arch/x86/`，与硬件无关的内存解析、状态转换放在 `kernel/`。

## `arch/x86/idt.c` 与 `include/arch/x86/idt.h`

- 负责：安装 32 位 IDT、解释异常现场、输出寄存器及 #PF 诊断。
- 接口：`idt_init()`、不会返回的 `exception_dispatch(frame)`。
- 数据：8 字节 packed gate、6 字节 IDTR 操作数、`exception_frame`。
  IDT 有 256 项，0..31 为 DPL 0 的 interrupt gate，其余保持不 present。
- 协作：ISR 汇编提供入口地址表；异常报告调用串口，最后调用 `panic()`。
- 测试：真实 #UD、#DE、#GP、#PF；核对向量、错误码、故障指令地址与停止路径。

## `arch/x86/isr.S`

负责归一化 CPU 栈帧并保存通用寄存器。i686 的 8、10、11、12、13、14、17 号异常
由 CPU 压入错误码，其余入口补一个零。不能为 #PF 再补一个零，否则 C 会把错误码当 EIP。
这里针对 i686，不能把现代 CET 等新增异常的栈约定直接套进来。

```text
frame -> EDI ESI EBP saved_ESP EBX EDX ECX EAX
         vector error EIP CS EFLAGS
```

`pushal` 中的 saved_ESP 是保存现场前、指向 vector 的栈地址；加上 20 字节才是
发生异常前的 ring 0 ESP。汇编清除 DF，重载 DS/ES，按 16 字节规则对齐调用栈，
把原现场指针传给 C。所有处理都终止，所以没有伪造恢复路径或执行 `iret`。
目前只支持同特权级入口；未来 ring 3 会额外产生 SS/ESP，必须扩展实现和测试。

## `kernel/panic.c` 与 `include/kernel/panic.h`

`panic(reason)` 输出原因和 `HALTED`，然后进入不会返回的 `kernel_stop()`。
后者以 `cli; hlt` 循环停止执行，也是测试验证明确停止路径的 GDB 断点。
正常启动最终也调用 `kernel_stop()`，但不会输出 PANIC。
没有紧急备用栈，异常栈自身损坏时无法保证报告；这不是完整 double-fault 恢复设施。

## `kernel/memory_map.c` 与 `include/kernel/memory_map.h`

- 接口：`memory_map_parse(bytes, length, map)`；`memory_map_contains_ram(map, begin, end)`。
- 数据：最多 128 个 `memory_region`，保留 64 位 `[begin,end)` 和原始 type。
- 解析：按字节读 little-endian 字段，不依赖记录对齐；记录的 size 不包括自身 4 字节。
  接受扩展字段并跳过，不假定所有条目固定为 24 字节。
- 拒绝：空输入、短记录、越界长度、零长度区域、64 位地址溢出、容量耗尽；失败后 count 为零。
- 区间查询：可用区可以相邻、乱序；任何非 type 1 的重叠区都覆盖“可用”声明。
- 测试：宿主 ASan/UBSan 测试直接链接同一份 C 实现；QEMU 损坏引导信息测试再次覆盖真实入口。

## `kernel/boot_info.c` 与 `include/kernel/boot_info.h`

`boot_info_read(magic, address, boot)` 检查 Multiboot 交接，把地图交给解析器，
收集永久保留区并复制命令行。`boot_context` 含地图、最多 32 个保留区、128 字节命令行。
它不分配页、不构造页表。失败后的 context 不可继续使用；调用方必须停止。

支持基本内存、启动设备、命令行、最多 8 个模块、内存图、加载器名称。
模块的描述符、数据和字符串都保留。符号表、驱动表、APM、VBE、framebuffer 等尚未支持，
出现相应标志时明确拒绝，防止遗漏其指针指向的数据后误分配。
字符串必须在界限内终止；引导结构、地图和模块指针都检查范围/加法，并要求其数据位于 RAM。

**加载器仍是可信计算基的一部分。** 分页开启前没有独立机制验证“任意物理指针背后确实有 RAM”；
内存图地址本身只能先做低 64 MiB 数值界限检查，读出地图后再交叉验证。
这些检查拒绝可检测的损坏，并不声称能防御恶意加载器伪造整个硬件地图。

测试包括实际 `-initrd` 模块的数据完整性、保留和映射；以及错误 magic、越界指针、
缺少内存图标志、坏记录、模块计数和未终止字符串的安全拒绝。

## `kernel/physical_pages.c` 与 `include/kernel/physical_pages.h`

接口：初始化、查询状态、早期 `physical_page_alloc/free()`、统计可用页数。
静态数组为低 64 MiB 的每个 4 KiB 页记录一个字节状态：

| 状态 | 含义 | 可由 free 归还？ |
| --- | --- | --- |
| UNAVAILABLE | 未证明可用或追踪范围之外 | 否 |
| RESERVED | 固件、引导数据、非可用区域等 | 否 |
| FREE | 完整的可用 RAM 页 | 不适用 |
| KERNEL | 内核映像，包括页表、状态表、栈、IDT | 否 |
| ALLOCATED | 内核早期接口临时取得的页 | 是 |

只把 type 1 中的完整页面开放，再以非可用区、低 1 MiB、引导数据和内核范围覆盖。
保留区碰到半页也保留整页，不能用向内取整漏掉边缘。超过 64 MiB 的 RAM 不参与分配。
所有输入验证完才修改状态，初始化只允许成功一次。

当前是单 CPU、IF=0 的早期线性扫描，无锁、无堆、无碎片整理。
归还要求页对齐且状态必须为 ALLOCATED，因此拒绝永久保留页和双重释放。
它不清零数据，也没有域身份、代数或授权检查，不能当作用户资源所有权接口。
未来跨域交付必须补上清零、授权及映射生命周期同步。

测试：宿主穷尽分配、保留重叠、未对齐边缘、上限裁剪、重复分配和重用；
QEMU 对真实引导地图与实际内核范围重复验证保留和耗尽行为。

## `arch/x86/paging.c` 与 `include/arch/x86/paging.h`

`paging_init(boot)` 建立一页目录、16 页页表并启用分页；结构都 4 KiB 对齐、
静态存放在内核 BSS，因此已被页追踪模块永久保留。
最多映射低 64 MiB 中的 RAM、内核页和必需引导数据，页零始终不存在。
非 RAM 孔洞默认不映射；整个 64 MiB 以外没有 present PDE。
所有映射均为 supervisor，内核代码和只读数据页去掉 RW，其他映射允许内核写。

`paging_unmap_allocated(address)` 只接受早期临时取得的页，清掉 PTE 后执行 `invlpg`。
不会解除永久内核/引导映射，也不会自动归还物理页。
此接口不是通用映射授权 API；当前单一内核地址空间的 identity mapping 仍覆盖可用 RAM。

测试：GDB 读取真实 CR0/CR3、全部 PDE/PTE、内核与引导页状态；
实际 null/越界读、只读页写入、预热 TLB 后解除映射的 #PF。

## `kernel/selftest.c`、`arch/x86/test_faults.S`

负责显式选择的内核诊断，不参与正常资源分配策略。C 测试页状态，汇编执行指定故障指令，
导出故障位置标签供测试核对 CPU 报告的 EIP。用汇编做非法访问可以避免 C 编译器
把空指针解引用当作未定义行为改写为别的指令。
正常启动无参数时不执行破坏性测试。

## 集成与既有文件

`kernel/main.c` 只编排初始化与必要的运行时检查；串口新增 `serial_hex()` 用于寄存器诊断。
链接脚本新增页对齐的 `__kernel_readonly_end`，避免只读内容与可写 BSS 共用页面。
Makefile 改用统一的 C/汇编对象规则，保留原五个目标，并加上宿主测试目标 `test-memory`。
`tests/phase1.py` 仅扩展启动辅助函数以支持独立日志、启动参数和 initrd，旧断言未放宽。
Phase 1 的 `boot.S`、端口 I/O 原语和 `tests/entry.gdb` 均未改动。
