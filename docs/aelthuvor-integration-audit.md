# Aelthuvor Phase 1 / Liberums Phase 6 实际集成审查

**结论：未修改的 Aelthuvor Phase 1 ELF 能在当前 Liberums Phase 6 的默认合作 profile 下实际运行，ABI 1.1→1.2 兼容、First-fit/Best-fit、资源生命周期及跨域拒绝均有本次 QEMU/GDB 证据。原有集成命令存在失败，不能标为“全部通过”。原 ELF 不支持抢占 profile，已验证其被正确拒绝。**

审查执行于 2026-11-16（Asia/Taipei；UTC 2026-11-15，详见证据清单）。本次没有修改、提交或推送 Aelthuvor；Liberums 生产源码、历史测试和 SDK 均保持基准版本。仅新增 `tests/audit_aelthuvor.py` 和审查文档/文本记录。

## 版本、来源与工具链

| 对象 | 本次实际版本 |
|---|---|
| Liberums main/HEAD（开始时远程及本地） | `85a493466afe2038ca22a9870f6f9ce37dd7f3a8` |
| Aelthuvor main/HEAD（开始时远程及本地） | `b10d337d254b18e98204990203a77a215c82c568` |
| Aelthuvor vendored SDK 来源 | Liberums `49d5dbf2e78f7ad7ed6ec4acdeece68b1b6568d0`，见其 `sdk/manifest.json` |
| LibOS 编译 SDK / ELF note | ABI 1.1，required features=29（PAGES、ELF32、COOPERATIVE、LIMITS） |
| 当前内核运行 ABI | 1.2，即 `0x00010002` |
| 默认 profile / 抢占 profile features | 31 / 119；后者去除 COOPERATIVE、增加 PREEMPTION/REVOCATION |
| 编译 / 链接 / Python | GCC 14.2.0、GNU ld 2.44、Python 3.12.14 |
| 仿真 / 调试 | QEMU 10.0.13（Debian 1:10.0.13+ds-0+deb13u1）、GDB 16.3（Debian 16.3-1） |
| 机器参数 | `pc`、TCG、`qemu32`、单 CPU、32 MiB、无 NIC、headless |

环境原来没有 QEMU/GDB，且无 sudo/package helper。本次仅在 `/workspace/work/tools` 下载并解包 Debian 仓库包，通过 LD_LIBRARY_PATH 和 QEMU firmware 搜索目录运行，没有更改系统包配置。包名/版本保存在 [toolchain-packages.txt](audit-evidence/toolchain-packages.txt)。构建产物的 debug 路径与工具版本会影响 ELF hash，不宣称跨工具链 bit-identical。

SDK 固定摘要（校验未被删除）：

| 文件 | SDK SHA256 | 与当前内核公共头相同？ |
|---|---|---|
| `elf.h` | `2fcfab45adc03ee39169f36b7d43068ad5dd2c008b2b686f94ae137f8e659e5c` | 是 |
| `resource.h` | `9641ad44d3b036019c80ca7d75e85eb6a4347184b139f345279fa60c3772a298` | 是 |
| `syscall.h` | `cd8763a11e815807de7eb4f4b0e2289ddb2271d41f09718973c4210f18c4ad93` | 否：minor、event.h include、新 feature/profile 定义 |

`make check-sdk` 及其 SHA256/文件集测试通过；`make check-sdk SDK_SOURCE=/workspace/work/sdk-source49` 对精确旧来源通过。指定当前 Phase 6 源目录则失败，因为该选项同时要求 pinned source 和工作头相同。这是来源更新约束，不能等同于二进制 ABI 不兼容。

## 本次执行命令与状态

工作目录：`/workspace/work/Liberums`、`/workspace/work/Aelthuvor`。两个仓库均由用户给出的 GitHub HTTPS URL 独立 clone，先执行 `git rev-parse HEAD` 和 `git ls-remote origin HEAD`；无本地用户修改。下表所有状态均为本次执行结果。

| 仓库 / 命令 | 结果（进程退出码） | 证据 |
|---|---|---|
| Liberums `make clean` | 通过 0 | `Liberums-make-clean.log` |
| Liberums `make -j2` | 通过 0 | `Liberums-make--j2.log` |
| Liberums `make test` | 通过 0 | `Liberums-make-test.log`，含真实 Phase 1–6 guests |
| Liberums `make test-phase6` | 通过 0 | `Liberums-make-test-phase6.log`，10 guests / 20 lifecycles，宿主边界套件 2 |
| Aelthuvor `make clean` | 通过 0 | `Aelthuvor-make-clean.log` |
| Aelthuvor `make` | 通过 0 | `Aelthuvor-make.log` |
| Aelthuvor `make test` | 通过 0 | `Aelthuvor-make-test.log`；4 SDK drift rejection tests，77,438,415 host assertions / 132 mock calls |
| Aelthuvor `make integration LIBERUMS=/workspace/work/Liberums` | **失败 2** | `Aelthuvor-make-integration.log`；`public ABI drift: syscall.h`，guest 尚未开始 |
| Liberums `make test-libos AELTHUVOR=/workspace/work/Aelthuvor` | **失败 2** | `Liberums-make-test-libos.log`；传播同一个入口失败 |
| 直接 `python3 tests/phase5.py --aelthuvor /workspace/work/Aelthuvor/build` | **失败 1** | `original-libos-gdb.log`；已执行到 stage 2，旧 oracle 要求 `0x10001` |
| 独立 `python3 tests/audit_aelthuvor.py --aelthuvor /workspace/work/Aelthuvor` | 通过 0 | `audit-aelthuvor.log`；15 guests / 30 lifecycles，详见下节 |
| `make check-sdk SDK_SOURCE=/workspace/work/Liberums` | **失败 2（预期来源不一致）** | `sdk-source-current.log` |
| `make check-sdk SDK_SOURCE=/workspace/work/sdk-source49` | 通过 0 | `sdk-source-pinned.log` |
| 无 sibling 仓库的导出目录分别 `make -j2` / `make` | 两者通过 0 | `independent-kernel.log`、`independent-libos.log` |

上述文件均在 [audit-evidence](audit-evidence/README.md)。本次保留原始失败，不以新脚本的成功覆盖原集成入口状态。`make test` 的 Phase 5 为 52 个 guests、49 个 parser cases；Phase 6 在 make test 和显式 test-phase6 中各执行一次。计数是各套件自己的范围，不将重复运行合并包装成独立覆盖率。

本环境每个涉及 VM 的命令实际追加：

```sh
QEMU=/workspace/work/tools/bin/qemu-system-i386 GDB=/workspace/work/tools/bin/gdb
```

这些 wrapper 仅提供解包工具的库/固件位置。标准已安装工具环境可直接重跑：

```sh
git clone https://github.com/Evan-Windproof/Liberums.git Liberums
git clone https://github.com/Evan-Windproof/Aelthuvor.git Aelthuvor
# 在所需历史版本的独立 checkout 中执行；不要覆盖已有工作树。
cd Liberums
make clean
make -j2
make test
make test-phase6
cd ../Aelthuvor
make clean
make
make test
make integration LIBERUMS=../Liberums  # 当前已知入口失败，并构建所有六个 ELF 变体
cd ../Liberums
python3 tests/audit_aelthuvor.py --aelthuvor ../Aelthuvor
```

审计脚本固定审核过的 Aelthuvor SHA，检查 Liberums 内核/ABI/辅助脚本与上述基准一致，允许仅文档/审计新增的后继提交。SDK 精确来源对象必须存在本地 Git history。六个 Aelthuvor ELF 需先按原 Makefile 构建；脚本只读取它们，记录前后 hash。它不替代一般版本兼容测试框架。

独立构建额外用了两个无 Git/sibling 的导出目录：`git archive` 到 `work/standalone/kernel`、`work/standalone/libos` 后分别 make；它们不需要彼此的路径或私有头。Aelthuvor integration 依赖内核的 `tests/phase5.py` / `libos.py` 是显式测试依赖，不是核心构建依赖。

## 真实外部 ELF 与启动方式

实测为 Aelthuvor 自己的编译器/链接命令产生的 ELF32/i386 ET_EXEC，含代码、rodata、数据/BSS、ABI PT_NOTE，由 `-initrd` 通过 QEMU 的 Multiboot handoff 交给内核。不是内核链接进去的 LibOS 对象，也没有更换 Aelthuvor SDK 或指令字节。标准对应命令：

```sh
make debug QEMU_INITRD=/workspace/work/Aelthuvor/build/first.elf \
  QEMU_APPEND=external GDB_PORT=1234
gdb -q -nx -batch build/liberums.elf \
  -ex 'set architecture i386' -ex 'target remote 127.0.0.1:1234' \
  -x build/phase5-aelthuvor-first.gdb
```

测试实际分配空闲 loopback GDB 端口，并为每个 case 启动新 QEMU。`make debug` 的完整 QEMU 参数和真实串口输出已保存；GDB 使用硬件断点，直接读寄存器、页表、物理 backing、资源表和用户 metadata，不调用内核函数来代替用户操作。

审计脚本与历史 suite 的差别明确限定：继续运行原 SDK 校验并逐字节比对 `git show 49d5...:include/abi/*`；仅将历史 GDB 的运行版本断言 `==0x10001` 改为精确 `==0x10002`，保留 feature、CPL3、PTE、allocator、ownership、peer、清零和回收全部断言。另增加合作模式 IF/preemptions 断言、原 ELF 抢占拒绝与 hostile-peer 读写。没有删除哈希校验、改源文件、改 ELF note 或弱化 guest 成功条件。

六个本次 ELF hash 记录于 `audit-aelthuvor.log`；例如 first 为 `777517549199e9e8e4c74d4881952bfea1005f9f4320b48040c57c528ccd631b`，best 为 `8c3f769ed50598b8f76166f85b5d827de541a4aef97092e79fa6d58b677ca590`。它们带绝对 debug 路径，仅是本次证据身份。

## 分层验证结果与 QEMU/GDB 证据

| 层次 | 本次证据 | 结论 |
|---|---|---|
| ABI 查询/兼容 | ELF note 要求 1.1/29；运行 probe 返回 `0x10002`、features 31、quota=4、bindings=4；首次 syscall 为 ABI_QUERY，接着 RESOURCE_LIMITS | **通过**，不是只看启动 banner |
| 外部装载 | program headers、file bytes、BSS/padding 清零、代码 R/O、栈/PDE/PTE、实际入口逐项读取 | **通过**，内核没有 LibOS 内部符号依赖 |
| 实际 Ring 3 | `_start` 及 `ael_checkpoint` 处 CS=0x1b、SS=0x23、私有 CR3、用户栈范围；syscall 处验证 TSS 内核栈及外层用户 ESP/SS | **通过** |
| 分配算法 | first policy=0、best policy=1；两者执行同一 `ael_memory_checks`，96-byte 分配分别必须选择先前 256-byte 洞 b / 更小 128-byte 洞 d（`tests/memory_checks.c:24`）；stage 2 只能在检查成功后到达 | **通过**，不是仅检查 policy 字段 |
| 分配/复用 | 无效/溢出请求、16 字节对齐、内部指针拒绝、重复 free、合并为整页、配额耗尽、整页清零与数据、256 个最小块复用 | **通过**，同一 C 行为检查同时在宿主 mock 和 guest 执行 |
| 真实映射/访问 | 活页为 `0x09000000`，1024 个 words 为 `0xa3110000|i`；PTE 指向真实 PFN，rights=47、owner epoch/generation 正确 | **通过** |
| 正常释放 | checkpoint 3 时 heap.live=0、pool.owned=0、resource_count=0、dynamic_table=0 | **通过** |
| 退出/异常完整回收 | normal、exit-live、非法地址 fault、只读代码写 fault；资源/动态表/image/stack/peer pages 全部 zero/free，free count 恢复；重复两次生命周期 | **通过** |
| peer 无权限 | 正常 peer 私有 CR3 下资源窗口无映射；额外实际 read/write 该活页 #PF，LibOS 正常完成 | **通过**，详见下文 |
| 负面兼容/回滚 | bad major / unknown feature 拒绝 -93；runtime major=2 在用户态 probe 失败后退出 93；OOM 实际分配 5 页后回滚清零 | **通过**，分别验证 loader 和 runtime 两层 |

`phase5-aelthuvor-first-gdb.log` 的本次原始摘录：

```text
CPL3: EIP= 0x8000000 ESP= 0x83ffffc CR3= 0x145000
AUDIT ABI: version= 0x10002 features= 31
RESOURCE: PFN= 0x157 PTE= 0x157067 data= 0xa3110000
LIBOS: policy= 0 live=1 VA= 0x9000000 PFN= 0x157 generation= 4
...
LIBOS: policy= 0 live=1 VA= 0x9000000 PFN= 0x157 generation= 8
PASS: external image, stack, tables and dynamic resources reclaimed
```

PTE 的 0x60 是硬件 A/D 位，不是额外用户权限。PFN、CR3、generation 为本次观察值，不是接口常量。

新增四个 hostile-peer guests（first/best × read/write，每个两轮）只用 GDB 在执行前设置 **peer 自己用户数据页** 中已有实验模式和目标 VA，不改代码或内核可信资源状态。primary 已完成策略检查、保有完整 marker 活页并 yield，peer 在其私有 CR3 下实际攻击该 VA：

```text
AUDIT PEER PF: vector=14 error=4 CR2=0x09000000 EIP= 0x800024c
AUDIT PEER PF: vector=14 error=6 CR2=0x09000000 EIP= 0x800024f
AUDIT PEER: Aelthuvor exit=0; hostile peer faulted; all pages zero/free
```

两个 PF 来自各自 read/write case，非同一次访问。primary 在 peer fault 后继续校验 1024-word 数据并退出 0；该实验同时检查对方失败不会破坏 LibOS。记录为 `audit-aelthuvor-{first,best}-peer-{read,write}-gdb.log`。

独立审计总计：9 个现有联动 cases / 18 lifecycles，2 个原 ELF 抢占拒绝 cases / 4 lifecycles，4 个 hostile-peer cases / 8 lifecycles。15 个新 VM、30 个生命周期；不得将其中 4 个被拒绝生命周期称为 Aelthuvor 抢占执行成功。

## Phase 6 影响：旧 ABI 与新功能分开

| 问题 | 判断与证据 |
|---|---|
| ABI 1.1 程序在 ABI 1.2 能否运行？ | **能，有条件：major 相同、minor 足够、requested features 全部存在。** Aelthuvor 原 ELF 在默认合作 profile 实测；Phase 6 另有不重新编译的 immutable ABI 1.1 fixture hash 校验及运行。 |
| 默认合作模式是否保留旧行为？ | **是，已测。** 返回 features=31，用户 IF=0、preemptions=0；First/Best 数据、显式 yield、释放和退出行为均通过。 |
| 旧内存 API 语义是否保持？ | **在当前合作路径和覆盖矩阵内保持。** `resource.h` 完全相同，claim/map/unmap/release 旧编号未变；无新增请求时旧 pending 分支不会介入。不能扩展为所有负载都形式化等价。 |
| 原 Aelthuvor 是否能在抢占模式运行？ | **不能按原始契约运行。** ELF requires=29 含 COOPERATIVE(8)，preempt features=119 不含8；first/best 在分配 image 前返回 -93，peer 完成且无泄漏。未擅改 note 或应用 feature 检查。 |
| Phase 6 新功能是否有效？ | **由内核专用 fixtures 验证有效。** spin、ACK、force、release、exit-pending；外部 preempt fixture 非 Aelthuvor。它们不能冒充 LibOS 新功能兼容测试。 |
| 撤销会否损坏现有 LibOS metadata？ | **条件性风险，源码推断。** metadata 位于 image BSS，当前不属于可撤销动态资源，通常不会直接被 unmap；但指向的 heap payload 会失效，owned/mapped/arena.used 仍旧有效状，alloc 可能返回已失映射地址，release 可能 ESTALE 且保留旧记录。没有做原 Aelthuvor 强制撤销实验，因为其 profile gate 正确拒绝。 |
| 抢占本身是否要求 allocator 加锁？ | 跨域抢占只暂停本域；单线程、私有 heap 在恢复后继续，不自动产生并发 metadata 写入。若新增同域线程、异步事件/upcall 或重入，则必须设计互斥/安全点/不可重入区和事件排队。不要把每次抢占都说成 heap race。 |
| Phase 2 是否需要撤销处理？ | 若目标包括抢占/压力协作，则需要 POLL/INFO/ACK、完整身份检查、pending/forced loss 状态和活块处理策略。只有合作模式的算法增强可不调用新 API，但不能声称兼容新机制。 |
| SDK 是否必须升级？ | 仅运行合作旧程序**不必**升级，1.1 已验证。使用新事件 API 则应将新 SDK 作为单独可审查更新：精确 commit、全部公共头（含 event.h）、SHA256、工具链版本和旧 binary 兼容回归。不能只改 minor 宏或去掉 hash。 |

Phase 6 的 [phase6-spin-gdb.log](audit-evidence/phase6-spin-gdb.log) 实测完整异步现场恢复、非合作循环的 peer 进度；[phase6-force-gdb.log](audit-evidence/phase6-force-gdb.log) 实测两个 alias 撤销、deadline 后 zero/free、同 PFN 新 owner 重用及旧 owner stale write #PF；`phase6-ack/release/exit-pending-gdb.log` 分别证明 ACK 和取消路径。宿主 revoke suite 另覆盖旧 ACK、错误 owner/epoch/generation、pending transfer、队列满及计数上界。本次未增加真实压力来源或 LibOS event handler。

例如第二次显式 Phase 6 运行的外部抢占 fixture 记录：

```text
PHASE6 EXTERNAL: preemptions 702 peer 3 IRQs 702
PHASE6 EXTERNAL: preemptions 318 peer 3 IRQs 1021
```

这是内核外部 fixture 的两轮，**不是 Aelthuvor**；计数受仿真速度影响。

## 阻塞、未执行项与复现性边界

已确认阻塞为原集成入口的 current-header byte equality 与历史 GDB 精确 1.1 返回值，属于测试框架未适配新内核；本次没有将它们改成通过。建议在未来授权的 SDK/测试维护中分别保留 integrity/provenance 和 feature-based runtime compatibility 两组检查。

未执行或不支持：原 Aelthuvor 在抢占下的成功执行、其撤销 ACK/超时恢复、真实系统压力回收、同域多线程、两个完整不同 LibOS 并发、硬件裸机、SMP、NX、DMA/IOMMU、任意中断嵌套、长时模糊测试、稳定墙钟期限与性能基准。本次也没有复现合作模式恶意无限循环；可用性限制来自 IF=0 和调度路径源码。

完整源代码位置、安全前提和风险评级见 [架构报告](exokernel-architecture-audit.md)。证据清单记录命令状态、ELF/SDK 身份及文本文件 SHA256；原 Aelthuvor 仍为指定基准、tracked files 干净。下一阶段只给出建议，见 [next-phase-recommendations.md](next-phase-recommendations.md)。
