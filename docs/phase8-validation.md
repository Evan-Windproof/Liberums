# Phase 8：实际验收与复现

日期 2026-11-16；Liberums2 云端恢复环境，i686 PC / TCG / 单 CPU / 32 MiB。
工具为 GCC 14.2.0、Binutils 2.44、QEMU 10.0.13、带 Python 的 GDB 16.3。
修改前 Liberums HEAD 与远程 main 均为 `5d9c8f35a4ae54ef08b93b7f4d5fecd9e350acbf`。
实际执行日志、GDB 地址证据、结果 JSON 和渲染截图见 [归档](phase8-evidence/README.md)。

## 复现命令

```sh
# 此云环境的工具路径；普通开发机无需此行
export PATH=/workspace/.liberums-tools/bin:$PATH
make -j2
make test-domain-pool test-phase6-native
make test-phase8
make test
# Aelthuvor 必须独立、保持指定版本；预先按其自身说明构建：
python3 tests/phase8_ael.py --aelthuvor /path/to/Aelthuvor/build
make -C /path/to/Aelthuvor integration LIBERUMS="$PWD"
make -C /path/to/Aelthuvor console-integration LIBERUMS="$PWD"
# 历史兼容另用对应固定 checkout：
make test-vga-compat AELTHUVOR=/path/to/historical-Aelthuvor
```

Aelthuvor Phase 3 固定 `46c7284325cdb04f7f6fd6a96377cf8ab9721a67`，
Phase 2 固定 `1d288be418ddeef89462914769d5ea84bb557ade`。
ABI 1.1 历史 suite 使用原固定来源，其 manifest/ELF hash 校验保持。
`phase8_ael.py` 本身不构建或修改外部仓库。
开发中只按外部仓库自己的 Makefile 构建产物；三个 checkout 的 tracked status 均保持干净，
没有修改 SDK、提交历史或推送 Aelthuvor。Phase 3 三个并存 ELF 的前后 SHA256 记录在 JSON。

## 结果与计数

| Runner | 实际结果 |
|---|---|
| 修改前旧 `make test` | 通过；第一次 Phase 4 观察停点失败与修正后通过日志分开归档 |
| 最终 `make test` | 退出 0，Phase 1～7.3 全部原回归及 Phase 8 均执行 |
| `make test-phase8` | 19 次 QEMU 启动：18 个 GDB 硬件状态审计 + 1 个实际 make run-multi 启动/完成检查；初始成功装载 58 个域生命周期 |
| 域池 native | ASan/UBSan，真实域池代码，模拟 ELF/RAM/硬件端点；64 轮复用、MAX Epoch、脏槽、容量、回滚通过 |
| 四域复用 QEMU | 单次启动中 24 轮 EXIT/#UD 交替，槽 1 Epoch 1→25；另外三域持续持有各四页 |
| Aelthuvor Phase 3 memory | 13 guests / 26 lifecycles；First-fit、Best-fit、抢占、ABI 1.1、失败/隔离/回滚 |
| Aelthuvor Phase 3 Console | 9 guests，11 完成 lifetimes + 1 持续 Console 会话，由宿主停止；真实 PS/2 输入/故障/独立设备撤销 |
| Aelthuvor 四域并存 | 3 个 GDB/QEMU guests：两种堆策略的抢占 ELF、Console 分别与三个独立 worker 并存，均完整回收 |
| Aelthuvor Phase 2 | 13 guests / 26 lifecycles，通过 |
| 历史 ABI 1.1 Aelthuvor | 15 guests / 30 lifecycles，通过，源码/ELF 字节保持 |

计数来自实际 runner 的完成日志和结果 JSON。64 次宿主模拟不算硬件测试；
多次重跑同一套件不叠加成覆盖率。19 个 Phase 8 guests 不全部声称为 GDB 审计，
持续会话也不计作已完成回收的 lifetime。58 是初始装载数，不把后续复用或失败回滚混入。

## 保护验收覆盖

| 范围 | 实际检查 |
|---|---|
| 装载 | 2/3/4 域；5/8 模块槽位耗尽；坏 ELF、坏 ABI 位于有效模块之间；只剩五页的部分 OOM；失败后后续模块成功 |
| 映像 | 每模块独立校验，逐字节文件/BSS、RX/R/RW PTE、四页零栈、不同 CR3/PFN、supervisor 内核、模块页保留 |
| 调度 | 四域真实 CPL3、IRQ0 原现场与恢复、7 GP、4 段、ESP/EFLAGS、CR3/TSS，YIELD/EXIT/FAULT 后其他域继续 |
| 资源 | 四域各四页占满 16 项；额外 CLAIM 配额拒绝；PFN 唯一、PTE/引用计数/owner ID+Epoch 一致 |
| 转移 | 满配额拒绝保持原所有权，释放后转移/清零/代数增加/RECEIVE；终止或不存在目标拒绝；满 inbox EAGAIN；未领取 inbox 的退出和复用 |
| 隔离 | 他域有效句柄 EPERM、旧 Generation/复用句柄 ESTALE、他域物理 alias #PF(error=5)，其余域退出成功 |
| 撤销 | 四域各二事件填满八项，第九项 ENOSPC 不改资源；挂起 transfer EBUSY；退出取消/旧 ACK ESTALE；强制撤除 PTE 后 #PF(error=4) |
| 设备 | 默认 ACQUIRE EPERM、独占冲突回滚不撤销原 grant、PA/UC/PTE、EXIT 与 FAULT 后重新授权、旧句柄代数拒绝、队列清空、无跨域 VGA alias |
| 复用 | 每轮检查实际终止状态、资源归零、页表/映像/RAM 清零且 PAGE_FREE、上下文与内核栈清零、Epoch 增加、其余三域身份和资源不变、free count 无漂移 |
| 清理 | 所有域 UNUSED、全部 16 资源 FREE、8 事件 inactive、设备 grant/队列归零、IRQ0/IRQ1 各自 EOI 相等、物理 free count 恢复初值 |

例如 `phase8-registers-four-gdb.log` 逐域列出 CR3、TSS.ESP0、被抢占 EIP/EFLAGS；
`phase8-reuse-24-gdb.log` 逐轮记录 old/new Epoch、EXIT/FAULT、owned=16、free pages 和回收结果。
调试器只写测试程序自己的命令/对抗输入和显式 selftest 请求，不直接制造内核成功状态。
键盘测试使用 QMP input-send-event→模拟 PS/2→IRQ1，不使用 COM1 输入伪造事件。

ELF/PDE 比较忽略 x86 自动维护的 Accessed 位，U/S、地址和其他权限位仍严格比较。
IRQ 寄存器比较按既有安全 EFLAGS 掩码处理硬件调试 RF，IF/IOPL 另行检查。

## 旧测试观察问题与修正

基准第一次在 Phase 4 期待 domain 2 时重复看到 domain 1 的停点。
改在原用户函数已写入 stage 后的独立 NOP 观察，按实际域/stage 设断点条件，保留全部原始所有权、
PTE、代数、硬件 fault 和回收断言；后续所有期望阶段也设置条件。修正后旧完整基准通过。

最终回归的一次 Phase 7.2 测试遇到 GDB/TCG 重复报告同一 IRQ1 PC。
现在仅在中断序号及完整原始 frame/GP/段/CR3 一致时识别重复停点；
新 IRQ 必须序号连续，返回仍验证原 frame、全部寄存器和恰好一次 EOI。
用户观察新增私有递增序号与 NOP，不漏真实 POLL，不重新注入重复输入。
QMP Unix 路径可能先于 listen 出现，传输层在有限启动期限内重试 ENOENT/ECONNREFUSED。
这些都是观察/连接修正，未减少任何保护断言。失败日志保留，最终完整回归重新执行并退出 0。

## 窗口、截图和本地验证

COM1 成功、VGA 显存实际内容正确、QEMU 渲染成功、本地可见窗口是不同证据。
本环境 `qemu-system-i386 -display help` 只有 `none`，没有 GTK；
本阶段通过真实显存检查和 [四域 Aelthuvor Console 截图](phase8-evidence/ael-console-four-domains.png)
验证渲染，不能声称云端弹出了图形窗口。
用户已确认 Phase 7.1 在本地 WSL2+GTK 可见；Phase 8 的本地窗口/实体键盘仍需用户验证。

本地已有 GTK/WSLg 时，可将路径替换为各自 ELF：

```sh
make -j2 phase8-fixtures
qemu-system-i386 -machine pc -accel tcg -cpu qemu32 -m 32M -smp 1 \
    -display gtk -serial stdio -monitor none -nic none -no-reboot -no-shutdown \
    -kernel build/liberums.elf \
    -append 'multi-external multi-vga=1 multi-keyboard=1' \
    -initrd /path/to/Aelthuvor/build/console.elf,build/phase8/worker-a.elf
```

前台 Console 获得独占设备，后台 worker 无设备权限。没有新增终端复用或多 Console 同屏功能。
其余已知边界见 [安全审查](phase8-boundary-review.md)。
