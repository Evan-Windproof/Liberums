# Phase 9：实际验证记录

日期：2026-11-17（Asia/Taipei）。Liberums2 恢复环境，QEMU 10.0.13/TCG/qemu32/单 CPU/32 MiB，
GDB 16.3、GCC 14.2.0、Binutils 2.44。未重新配置或发布云环境。
开发前 HEAD/远程 main：`449c78f9b12535cded2bca575cd44e0b65a0953b`。
外部 Phase 4 HEAD/远程 main：`297d7a3e2852b495b66f4a2b585b461e1bcc28b7`。

## 实际执行

```sh
export PATH=/workspace/.liberums-tools/bin:$PATH  # 仅此云环境
make -j2
make test                                     # 修改前基线，修改后完整回归均退出 0
make test-phase6-native
make test-phase9-e2e PHASE9_AEL_BUILD=/tmp/liberums-phase9-ael/build
# 外部 checkout 本身不改源码、SDK 或历史：
make -C /tmp/liberums-phase9-ael phase4-integration LIBERUMS=/workspace/Liberums
make -C /tmp/liberums-phase9-ael integration LIBERUMS=/workspace/Liberums
make -C /tmp/liberums-phase9-ael console-integration LIBERUMS=/workspace/Liberums
# 对相应固定 Phase 2/3 及 ABI 1.1 checkout 分别运行原有 integration / console / compat。
```

本地可用同类工具并替换绝对路径；构建外部 SDK 的步骤见 [兼容性文档](phase9-libos-compatibility.md)。
`make test-phase9-e2e` 验证了真实 Makefile 入口，不只手工 Python 运行。

## 结果与统计边界

| 范围 | 日志中实际完成结果 |
|---|---|
| 修改前完整基线 | make test 退出 0；原 Phase 1～8 全部执行 |
| 修改后完整回归 | make test 退出 0；保留原 Phase 1～8，加 Phase 9 core；包含原 run 系列、串口、VGA/PS2 及设备安全测试 |
| Phase 9 core | 5 个真实 QEMU/GDB guests：EDX/未知 syscall、ID-only 目标复用、可信诊断拒绝、默认关闭、128 轮复用 |
| Phase 9 外部 E2E 入口 | 21 个真实 QEMU/GDB guests，包含上述 5 个和 first/best 各 8 个四域 RAM 撤销实验；不能将 core 再加一遍算 26 |
| 压力 | 单次启动 128 次槽 1 复用，EXIT/#UD 交替，Epoch 1→129；另三域保留各四资源，实际总 owned=16，每轮 free count、清零、终止状态、旧句柄和回收均断言 |
| native | timer 1008、revoke 1321 个断言；真实协议/资源代码，模拟 RAM/PTE/时钟；另有 ABI 常量/布局测试与旧内存/Capability/设备/域池 suite |
| Aelthuvor Phase 4 | 13 guests / 18 lifecycles 的 App SDK/Reference 集成；原 memory 13/26、Console 9 guests（11 完成 + 1 持续会话） |
| 历史兼容 | Phase 3 memory 13/26、Console 9（11 完成 + 1 持续）；Phase 2 13/26；历史 ABI 1.1 suite 15/30 |

数字来自实际日志与结果 JSON。重跑相同 suite 不叠加覆盖率；
编译/ELF 校验、实际 CPL3 运行、完整应用成功和资源回收分别断言，不能相互替代。
持续 Console 会话由宿主停止，不算完成回收的 lifetime。128 次是有界多轮压力，不是长期 soak 保证。

## 普通 RAM E2E 的直接证据

- 请求：真实 ID/Epoch、handle、request、当前 tick/deadline，事件 reason=PRESSURE。
- 主动路径：GDB syscall trace 包含 POLL、INFO 0/1/2/3 和准确参数的 ACK；ACK 时 tick < deadline。
- 强制路径：由实际 PIT tick 到期，forced 计数增加；两个别名的 PTE/PDE 消失、引用为零。
- RAM：原物理页 PAGE_FREE、1024 个字全部为零；随后真实 SPECIFIC/MAP 同 PFN 检查仍为零。
- 反例：旧 MAP/ACK ESTALE；失效旧指针在精确标记的 load 产生 CPL3 #PF(error=4)；
  另一实验由未修改的 Ael SDK 主动安全退出 96，不能算正常应用完成或透明恢复。
- 隔离：每个 RAM guest 中 Independent Reference 的自身对象/内容/回收断言完成且 EXIT 0；两 worker 也 EXIT 0。
- 交错：挂起请求时 EXIT/#UD、RELEASE 后 ACK、迟到 ACK；原 Phase 8 的旧请求/Generation/事件表耗尽仍执行。
- 收尾：四槽 UNUSED、全部资源 FREE、所有撤销事件 inactive、设备授权和队列清空，物理 free count 恢复原值。

GDB 还核对实际 CPL3、CR3、TSS ESP0、内核栈、EFLAGS/IOPL、资源 owner 与 Generation。
未通过调试器写 PTE、内核事件或 Ael 私有数据结构来制造结果。
精确 deadline−1/deadline 整数边界由 native 验证；真实 QEMU 验证时间经过 deadline，二者分开统计。

## 失败记录与修正

开发前基线通过。随后专门构造的 ABI 反例在旧实现中读到 KBD_RELEASE
EAX=ESTALE、EDX=0xcafe1234，与头文件承诺不符，GDB 严格断言失败。
修复后一行清零使同一反例通过。失败日志保留为 `abi-contract-before-failure.log`；
没有修改成功判定、删除旧测试或改 Aelthuvor 来掩盖问题。
本阶段没有通过弱化 IRQ 顺序/保护断言来改变旧测试。

## 归档与可见性

证据位于 [phase9-evidence](phase9-evidence/README.md)：完整 baseline/regression、专项结果、
128 轮日志、ACK/force/fault/交错 GDB+串口、外部兼容记录、内核/SDK/ELF 哈希和来源。
复用旧测试 transport，因此部分文件仍以 `phase8-phase9-` 命名；不是复用旧成功输出。
外部 runner 的 Git commit 字段在提交前记录基准 HEAD，实际运行二进制由 Kernel ELF SHA256 核对。

云端只支持 QEMU display none；本阶段完成串口、实际 PS/2 注入、VGA 显存和 QEMU renderer 回归，
不能宣称云端 GTK 窗口或实体键盘已验证。原本地 WSLg 操作方式保留，本阶段本地交互需维护者确认。
未实现内容和安全边界见 [边界审查](phase9-boundary-review.md)。
