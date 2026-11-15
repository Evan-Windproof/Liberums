# Phase 5 实际验收记录

> 以下主体为 ABI 1.0 基础设施首次验收的历史记录；本次 ABI 1.1 与双仓库结果见文末追加节。

日期：2026-11-15（Asia/Taipei）。以下结果来自实际构建、宿主解析器、QEMU 和 GDB。
本次只操作 Liberums，没有修改或提交 Aelthuvor 仓库。

## 基准与环境

开始时工作区干净，本地分支 work，origin 为 `https://github.com/Evan-Windproof/Liberums.git`。
实际 fetch 后 HEAD/origin/main 同为 Phase 4 的 `d3efffd359f3a25a7a28758c32622208ca64029a`。
先运行原测试建立基准，原 51 个 QEMU 场景及宿主测试均通过。

环境沿用 Debian 13/x86_64，GCC 14.2.0，Binutils 2.44，Make 4.4.1，
QEMU 10.0.13/TCG/qemu32/32 MiB/单 CPU，GDB 16.3。
工具位于 `/workspace/.liberums-tools/bin`，没有新增第三方依赖。

## 实际执行的命令

```sh
export PATH=/workspace/.liberums-tools/bin:$PATH
make clean
make -j2
make test
make
git diff --check
```

清理、完整重建、全套验收成功；再次 make 无需重建，diff 检查返回 0。
完整输出位于 `build/build.log`、`build/test.log`，生成物不提交。
此外实际执行了 `make external-fixtures`、独立 fixture Makefile、
`python3 tests/phase5.py --mode normal` 和完整 `python3 tests/phase5.py` 集成测试。

| 套件 | 实际结果 |
| --- | --- |
| 原宿主内存/capability | 原 10,905 条断言通过，ASan/UBSan 无报告 |
| Phase 1 | 原测试不改，2 个 guest 通过 |
| Phase 2 | 原测试不改，22 个 guest 通过 |
| Phase 3 | 原测试不改，17 个 guest 通过 |
| Phase 4 | 原测试不改，10 个 guest 通过 |
| 新 ELF 宿主检查 | 49 个有效/损坏输入，通过同一份 elf_validate 的 ASan/UBSan runner |
| 新 Phase 5 | 52 个独立 guest，各完成两轮生命周期，全部通过 |
| 全套 QEMU | 103 个 guest；没有必需项跳过 |

旧单页链接断言、旧用户映像/测试和六页路径保留，没有放宽旧断言。
新多页路径使用独立构造和预算，而不是取消原限制。

## 独立 SDK 与外部输入

phase5.py 实际将 `include/abi/` 复制到 `build/sdk/include/abi/`，仅用这个 SDK 路径，
在独立 Makefile 下生成 ELF；只链接 fixture 自身对象，不链接内核库/对象。
源码只包含公开 abi/ 头，没有 kernel/ 私有结构。外部文件通过 QEMU -initrd 提供，不嵌入内核。

五种原始 fixture：minimal、normal（多页）、fault（读内核）、code-fault（写代码）、runtime-abi。
另有去掉 section 表索引的有效输入、重复独立启动、加载 ENOMEM 和缺失模块，共 9 个基础 guest。
43 个损坏输入各另开 guest，因此总计 52。每个 guest 内又重复两轮，验证初始化与重用。

## 正常执行与多页证据

normal.elf 的实际 readelf -l：

```text
LOAD  off=0x1000 va=0x08000000 filesz=0x2006 memsz=0x2006 R E align=0x1000
LOAD  off=0x4000 va=0x08003000 filesz=0x13ac memsz=0x13ac R   align=0x1000
LOAD  off=0x6000 va=0x08005000 filesz=0x1388 memsz=0x36c8 RW  align=0x1000
NOTE  off=0x5388 va=0x08004388 filesz=0x24   memsz=0x24   R   align=4
```

实际包含三个代码页、两个只读页、四个数据/BSS 页，加四个栈页；目录/用户页表另占两页。
程序真正调用第三个代码页的函数，检查其返回值，读取跨页只读/初始化数据，
逐字节检查 9000 字节 BSS 为零后改写，并在下一轮再次验证初始值。

GDB 摘录：

```text
ELF: image+stack pages=13 CR3=0x143000 stack_top=0x08400000
CPL3: EIP=0x08000000 ESP=0x083ffffc CR3=0x143000
CPL3: EIP=0x08000330 ESP=0x083fff9c CR3=0x143000
RESOURCE: PFN=0x152 PTE=0x00152067 data=0xaabb0000
```

这些地址是本次链接/分配值，不是 ABI。测试根据当前用户 ELF 的符号和 LOAD 字段核对，
不是把某次地址写死为成功条件。

GDB 在执行前读取全部用户 PTE，逐段比较文件字节/BSS 零区，检查四页栈全零、
映像/栈物理页独立且状态 ALLOCATED，kernel PDE 全部 supervisor，未指定地址均无映射。
在真实外部入口/用户检查点验证 CS=0x1b、SS=0x23、用户栈范围、CR3。
在 ABI 查询 handler 检查 TSS 可信栈及 CPU 保存的外层 ESP/SS，调用号实际为 3。
同伴 B 也通过硬件断点验证 CPL=3 和不同 CR3，不是内核函数替代品。

## ABI 与资源 API

客户端查询 ABI 1.0/Feature Flags=15，通过公开兼容函数检查所需功能。
最小程序和多页程序都申请真实页、自己选择资源窗口 VA、读写、yield、unmap、release、exit。
GDB 检查所有者、引用、PFN、PDE/PTE 及实际页内容。多页客户端还申请到四页，
第五页得到 ENOSPC，证明外部映像没有绕过原资源配额。

不兼容要求分两层验证：

- ELF note 的 major、minor 或 required feature 不兼容时，加载返回 -93，未分配 A 的用户页。
- 合法 note 的 runtime-abi 客户端故意要求不同 major，自行正常 exit(93)，同伴仍 exit(0)。

第二项是预期的客户端兼容性拒绝，不记为普通成功程序，也不伪装成内核异常。

## 43 类损坏输入

实际覆盖 Magic、ELF64、小端标志错误、ident 版本、OSABI、机器、类型、e_flags、头大小，
程序头偏移溢出/与 ELF 头重叠/数量/项大小，文件偏移及 filesz 越界、filesz>memsz、零 LOAD，
内核/资源窗口/溢出 VA、memsz 溢出、非二次幂或超大 alignment、页内不同余，
LOAD 重叠及仅在页取整后重叠，W+X/不可读权限，无效/数据/BSS 入口，
INTERP/DYNAMIC/TLS，缺失或损坏 note，ABI major/minor/features，截断、超大文件及段数量预算。

每个输入既跑宿主解析器，又通过真实 Multiboot 模块进入内核加载路径。
期望的格式错误为 -8，ABI 要求错误为 -93；GDB 核对加载结果、A 未激活、失败前后空闲页数相等，
并确认 B 继续在 Ring 3 正常运行和结束。没有仅靠串口 rejected 文本判定。

有效的 no-sections ELF 清掉 section table 索引后仍运行，验证装载确实依赖 program headers。

## ENOMEM、用户异常与回收

external-oom 诊断在 B 准备完后暂存 RAM，只留五页，A 会在构造中实际取得目录、页表和三个代码页，
复制非零代码之后才遇到后续分配失败。GDB 确认 rollback_pages=5，五页全部清零、FREE，
count/live 为零，空闲数恢复失败前值；恢复暂存页后 B 仍正常运行。
这不是直接返回一个模拟 ENOMEM，也没有让失败输入修改用户页表。

两个独立故障 fixture 在留下动态资源绑定后触发硬件 #PF：

```text
读 supervisor 内核页：EIP=0x080003a0 CR2=0x00100000 error=5
写只读用户代码页：EIP=0x080003a0 CR2=0x08000000 error=7
```

测试比较 CPU 帧 EIP 与用户 ELF 的实际故障标签、CR2、错误码和可信内核栈。
故障 A 为 FAULTED，同伴 B 为 EXITED/status=0，不跳过故障指令。
原资源清理回收其未主动释放的绑定；随后销毁映像和栈。

回收点核对内核 CR3、无 current_domain、无 live space/动态表/资源引用。
快照中的所有私有基础页为 FREE 且整页为零，空闲总数恢复创建前值。
第二轮重复相同检查，包含 BSS/初始化数据/栈，防止上轮内容残留。

## 开发中失败与修正

抽取共用域上下文函数时，一次文本替换误改了 USER_CODE_SELECTOR，编译器正确拒绝；
恢复明确的内核选择子常量后重新构建并完成全部测试，没有将该轮构建计为通过。
最终没有未解释的 Panic、加载失败、回收泄漏或必需验收阻塞。

日志位于 `build/phase5-*-gdb.log`、`*-serial.log`。runner 取证后终止自己创建的进程组，
日志尾部 SIGTERM/Terminated 是清理。错误退出、超时和 GDB 断言失败都会使总测试非零。

## 限制与发布

只支持已文档化的静态 ELF profile、一个模块、64 个映像页、四页栈、单 CPU 合作式执行。
资源额度和低 64 MiB 限制沿用；无 NX/完整 W^X、SMP、动态链接、ELF 重定位、libc/POSIX、
用户 exec、文件系统、网络或 LibOS。没有在真实硬件执行，也没有修改 Aelthuvor。

完整测试后提交与推送 Liberums，并核对远程 SHA；最终报告提供实际提交和分支。
该文件不包含自己的提交 SHA，避免循环。Phase 5 完成后停止。


## 双仓库集成追加验收

本次任务中，远程 main 已是 `bdee0ad`（Phase 5 基础设施），并包含指定 Phase 4 基准
`d3efffd359f3a25a7a28758c32622208ca64029a`。工作区干净；fetch 后保留所有这些提交，未回退。
先完整运行当前基准，103 个 QEMU guest、49 个 ELF 宿主输入、原 10,905 条宿主断言均通过。

本次内核增加向后兼容的 ABI 1.1：只读额度查询号 4、LIMITS 特性位；
资源上限改为引用公开常量，未改变任何已有调用号、参数、所有权和页表保护。
原 minimal fixture 明确要求 1.0，仍在 1.1 内核上正常执行；损坏 minor 测试改为要求当前版本加一。
新增 `tests/libos.py`，由 `tests/phase5.py --aelthuvor <独立build目录>` 调用。

Aelthuvor 在独立仓库开发 C11 runtime、页面封装、块分配器和静态应用 ELF。
其 first-fit/best-fit 不要求内核区分算法；源码和库均未复制进本仓库。
公开 ABI 固定来源/哈希见该仓库 sdk/manifest.json。

实际执行并成功：

```sh
export PATH=/workspace/.liberums-tools/bin:$PATH
make clean
make -j2
make test
make
git diff --check
make test-libos AELTHUVOR=../Aelthuvor
```

Aelthuvor 侧也执行 clean/build/test/integration 与固定 SDK 来源检查。
环境为 Debian 13，GCC 14.2.0、Binutils 2.44、Make 4.4.1、QEMU 10.0.13/TCG/qemu32/32 MiB、GDB 16.3。
本机器原先没有 QEMU/GDB，经验证 Debian 签名索引和包校验下载、解包到仓库外工具目录，未关闭验证。

| 本次最终套件 | 结果 |
| --- | --- |
| 原 Phase 1–4 | 51 个 QEMU guest 全部通过；原 10,905 条宿主断言通过 |
| 基础设施 Phase 5 | 52 个 QEMU guest，49 个 ELF 宿主输入全部通过 |
| Aelthuvor 宿主 | 77,438,415 条断言，ASan/UBSan 无报告；另四类 SDK 漂移拒绝测试 |
| 双仓库集成 | 9 个 QEMU guest、18 个生命周期全部通过 |
| 本次最终 QEMU 合计 | 112 个场景；没有必需项跳过 |

新集成场景为 first、best、跨域故障、只读代码写故障、活跃资源退出、运行时 ABI 拒绝、
ELF note major 拒绝、ELF note feature 拒绝、实际部分加载 ENOMEM 回滚。
GDB 逐段核对外部 ELF/BSS/栈/PTE，入口和运行时 CS=0x1b、SS=0x23、私有 CR3；
在前两次调用核对真实 int 0x80 外层帧、TSS 可信栈及号 3/4。

分配后实际 PFN=0x155（first 本次构建）、VA=0x09000000、PTE=0x00155067，
资源 owner=1、epoch 匹配当前域、rights=47、references=1，1024 字为 0xa3110000|i。
peer B 无动态窗口映射，在不同 CR3 的 CPL3 下正常执行并 exit(0)。
策略差异由 guest 断言选择的空闲块地址，GDB 同时检查用户 policy=0/1 和内核实际资源状态。

跨域故障真实 CR2=0x08800000/error=4，代码写故障 CR2=0x08000000/error=7；
EIP 必须是外部 ELF 的故障标签，不跳过指令。故障与带资源退出之后，动态页、动态表、
映像和栈全部清零 FREE，空闲总数恢复。ENOMEM 必须实际取得五页并逐页验证回滚，不是模拟返回。
两轮均核对同伴正常、失败域未扩大权限；原 Phase 2 initrd 保留检查及单页链接断言继续通过。

日志为 build/build.log、build/test.log、build/libos-test.log、build/phase5-aelthuvor-*-gdb.log/
*-serial.log；Aelthuvor 也保留 build/build.log、test.log、integration.log。
基准另保存在 /workspace/validation-logs/baseline，不提交任何日志、生成物或工具。
无未解释失败；仅验证本次机器，未声称新任务恢复/发布或真实硬件验证。
限制不变：单 CPU 合作式、低 64 MiB、每域四页、同步撤销，无 NX/完整 W^X、抢占、SMP 或 POSIX。
双仓库分别提交并推送，最终交付报告核对远程 SHA；到此停止，不进入后续阶段。
