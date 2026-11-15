# Phase 3 实际验收记录

日期：2026-11-15（Asia/Taipei）。本记录基于当前云机器的实际构建、QEMU 和 GDB，
不是根据代码推断。没有将真实硬件、SMP、抢占或 LibOS 标记为通过。

## 基准与环境

开始前工作区干净，当前本地分支为 work，origin 为 `https://github.com/Evan-Windproof/Liberums.git`。
实际 fetch 后，HEAD 和 origin/main 都是 `ed7f26344fbea0fa4b9f249a381fc37a0f2b4c17`。
先完整复跑原 Phase 2 验收，通过后才实现 Phase 3。

| 工具 | 实际版本/设置 |
| --- | --- |
| 宿主 | Debian 13，x86_64，普通用户 |
| GCC / Binutils | 14.2.0 / 2.44，C11 freestanding，i686 |
| Make | 4.4.1 |
| QEMU | 10.0.13，TCG，qemu32，32 MiB，单 CPU |
| GDB | 16.3，回环 QEMU stub，硬件断点 |
| 宿主测试 | 原 memory_tests，ASan + UBSan |

QEMU/GDB 使用 Phase 1 已安装的 `/workspace/.liberums-tools/bin`，未添加第三方依赖。

## 命令与总结果

在仓库根目录执行：

```sh
export PATH=/workspace/.liberums-tools/bin:$PATH
make clean
make -j2
make test
make
git diff --check
```

完整清理、重新构建和全套验收均通过；增量 make 无需重建，diff 检查返回 0。
编译仍启用 `-Wall -Wextra -Werror -Wpedantic`，用户 ELF 和内核均实际生成。
最近一次构建与完整测试输出保留在 `build/build.log`、`build/test.log`，不会提交生成物。

| 套件 | 数量和结果 |
| --- | --- |
| 原宿主内存测试 | 10,890 条断言；穷尽 2,169 个测试页，ASan/UBSan 无报告 |
| Phase 1 | 原 ELF/Multiboot/入口/串口/GDB 断言全部通过，2 个 guest |
| Phase 2 | 原内存、分页、异常、模块、损坏输入测试全部通过，22 个 guest |
| Phase 3 正常测试 | 2 次独立普通启动 + 1 次部分分配失败回滚后正常启动 |
| Phase 3 故障测试 | 14 个独立 guest，故障隔离、正常同伴完成和两轮回收全部通过 |
| 合计 | 全套 41 个 QEMU guest；Phase 3 的 17 个 guest 各运行两个完整生命周期 |

没有跳过必需项。Phase 1/2 脚本及其断言均未修改。

## 真实 CPL、CR3 和特权级栈证据

`phase3.py` 在两个用户 ELF 的 `user_checkpoint` 地址停下，直接读取寄存器，
并在 `syscall_dispatch` 第一条指令处读取可信栈及 CPU 帧：

```text
TR =0028 00121040 00000067 00008900 DPL=0 TSS32-avl

USER1: CS=0x1b SS=0x23 EIP=0x8000020 ESP=0x8002fbc
       CR0=0x80010019 CR3=0x12b000 ESP0=0x123170
TRAP1: CPL=0 ESP=0x12311c FRAME=0x123134
       USER_ESP=0x8002fc0 USER_SS=0x23

USER2: CS=0x1b SS=0x23 EIP=0x8000020 ESP=0x8002fbc
       CR0=0x80010019 CR3=0x131000 ESP0=0x125200
TRAP2: CPL=0 ESP=0x1251ac FRAME=0x1251c4
       USER_ESP=0x8002fc0 USER_SS=0x23
```

CS & 3 = 3 证明真实用户级执行，两个不同 CR3 证明选择了不同页目录。
CR0 保留 PG/WP/PE，并增加 TS，禁止读取未虚拟化的 FPU 状态。
两个 ESP0 和 handler 栈均不同；FRAME=ESP0-60，包含真正跨级保存的用户 ESP/SS。
这些数值会随链接布局改变，不是 ABI；自动测试根据当前符号和实际页分配验证。

测试读取每个域完整的页目录/用户页表，核对：

- 两域十二个 backing pages 全部不同、页对齐、状态为 ALLOCATED。
- 低端内核 PDE 始终 supervisor；用户 PDE 仅有固定代码/数据/栈窗口和本域探测窗口。
- 用户代码 PTE 只读，数据/栈可写，未指定的用户 PTE 不 present。
- 两域相同 USER_DATA 对应不同物理页，初值分别 aaaaaaaa/bbbbbbbb，额外数据及栈清零。
- IDT 仅 128 号门为 DPL 3，TSS SS0 和 I/O bitmap 偏移正确。

## 系统调用与上下文

正常两域每轮通过实际 int 0x80 各执行七次调用，包含 yield、exit、日志、未知调用号和越界日志标量。
用户程序检查未知号返回 -38、日志参数 256 返回 -22，错误就用非零状态退出；GDB 检查两域均 status=0。
未知调用传入 0xffffffff 标量，不会被当作用户指针解引用。

汇编测试设置 EBX/ECX/EDX/ESI/EDI/EBP 的已知值，经过未知调用及 yield 后检查全部保持。
它主动设置 NT 与 DF，恢复后验证 NT 已清除、DF 保留，再 cld。GDB 检查 syscall/yield 计数，
防止测试没有实际执行却被当作通过。正常串口每轮输出 `ABab`。

## 十四个真实故障 guest

| 参数 domain-test= | CPU 故障 | 错误码 | 目标/意义 |
| --- | --- | --- | --- |
| kernel-read | #PF 14 | 5 | 0x00100000，user 读 supervisor 页 |
| kernel-write | #PF 14 | 7 | 0x00100000，user 写 supervisor 页 |
| cross-read | #PF 14 | 4 | A 读 B 私有窗口 0x08800000 |
| cross-write | #PF 14 | 6 | A 写 B 私有窗口 |
| cli | #GP 13 | 0 | Ring 3 执行特权指令 |
| io | #GP 13 | 0 | Ring 3 端口 I/O 被 TSS/IOPL 拒绝 |
| gate | #GP 13 | 0x1a | 软件 int 3 不能调用 DPL 0 门 |
| table-write | #PF 14 | 7 | 写本域页目录物理地址的 supervisor alias |
| physical-read | #PF 14 | 5 | 读同伴数据页物理地址的 supervisor alias |
| code-write | #PF 14 | 7 | 写本域只读用户代码页 |
| bad-stack | #UD 6 | 0 | ESP=0 后 ud2，仍用可信 TSS 内核栈 |
| cross-read + domain-reverse | #PF 14 | 4 | B 读 A 的 0x08400000 窗口 |
| cross-write + domain-reverse | #PF 14 | 6 | B 写 A 私有窗口 |
| fpu | #NM 7 | 0 | x87 fninit 被 CR0.TS 拒绝，无扩展寄存器泄露 |

每项在 `domain_fault` 第一条指令核对硬件帧 vector/error、故障 EIP 与用户 ELF 的汇编标签。
#PF 还直接读取 CR2，并在回收检查点再次核对内核记录。没有靠一条 Access denied 字符串判定。
非 #PF 的域记录中 address 为零，表示没有可用的故障内存地址，不解释陈旧 CR2。

一次跨域读的真实串口：

```text
USER FAULT domain=0x00000001 vector=0x0000000e error=0x00000004 eip=0x0800024c cr2=0x08800000
Bb
Domains: round completed, all pages reclaimed
```

两轮都看到故障域 FAULTED，另一域 EXITED/status=0。反向实验也成立。
用户栈为零的 case 检查真实保存 ESP=0，同时 handler 当前 ESP 位于可信内核栈。
故障指令从未被跳过或重试，故障域也未再次执行。

## 回收、清零与重复性

GDB 在 `domains_reaped` 核对当前 CR3 已恢复内核、current_domain 已清除。
两域目录和页表中的全部映射被清掉，十二页逐字读取为零，页状态为 FREE，空闲页总数恢复创建前值。
第二轮重新分配后再次检查私有数据、栈无残留，所有结果一致。
两个普通独立启动也完成相同检查。

分配失败诊断只留三页可用，要求六页的创建在中途失败，检查已申请三页清零、释放，
结构 count=0/live=false，且空闲数恢复。随后归还诊断暂存页，正常完成两域的两轮运行。
测试没有模拟成功返回或忽略创建错误。

静态域内核栈属于永久内核保留区，不作为动态用户页释放；这不是动态内核栈回收器。

## 调试中遇到的问题及修正

- 首轮 GDB helper 错把 C 函数符号作为整数读取，测试报 Cannot convert value to long。
  改为读取实际页状态数组和空闲计数快照，保留了逐页状态、内容、总数等回收断言。
- bad-stack 测试标签最初位于设置 ESP 的指令；审查时将故障标签放到真正的 ud2 上，
  确保 EIP 比较针对故障指令，未跳过现场核对。
- 隔离审查发现仅保存通用寄存器不足以开放 FPU。增加 CR0.TS 禁用路径及真实 #NM 测试，
  不以未实现的浮点状态切换作隔离保证。最终完整重建后所有测试通过。

测试证据取得后由 runner 终止自己创建的 QEMU 进程组；日志末尾 SIGTERM/Terminated 是清理。
意外退出、超时、GDB 非零和断言失败都会使总测试非零，不被当作通过。

## 已知限制与下一阶段

- 合作式、单 CPU、IF=0；恶意无限循环可阻塞 CPU，无抢占、公平性或强制 CPU 撤销。
- 固定两个域和六页布局；没有用户 ELF 加载器、动态进程、线程、TLS 或用户指针调用。
- 不开放浮点/MMX/SIMD；未来开放前必须实现完整扩展状态切换。
- 普通非 PAE i686 无 NX，用户数据/栈可能执行，不能宣称完整 W^X 或侧信道隔离。
- 低 64 MiB、Multiboot 数据容量/可选字段限制沿用 Phase 2，依然信任加载器。
- 无备用 double-fault 栈、SMP、异常恢复、LibOS、文件系统、网络或图形界面。
- 六页列表不提供委托权利、代数、Secure Bindings 或 Visible Revocation；这些留给 Phase 4。
- 未在真实硬件测试；当前必需的 QEMU/GDB 验收没有剩余失败或跳过项。

GitHub 发布按任务要求在完整测试后提交，并更新 origin/main；如远程基准变化则使用单独分支和 PR，
不使用 force。最终报告给出实际分支、完整提交 SHA 和远程核验结果；提交 SHA 不写入自身内容以免循环。
