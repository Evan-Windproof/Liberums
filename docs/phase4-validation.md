# Phase 4 实际验收记录

日期：2026-11-15（Asia/Taipei）。结论来自本机实际编译、QEMU、GDB 与宿主测试。
没有把只读源码、串口完成字符串或未执行命令算作通过。

## 基准与环境

开始时工作区干净，本地分支 work，origin 为 `https://github.com/Evan-Windproof/Liberums.git`。
fetch 后 HEAD/origin/main 均为指定基准 `8c2c2b10b167bf76b410798b07b346d1c22255d9`。
先复跑原 Phase 1–3 验收再开发；未覆盖用户改动。

| 项目 | 实际环境 |
| --- | --- |
| 宿主 | Debian 13，x86_64，普通用户 |
| GCC / Binutils / Make | 14.2.0 / 2.44 / 4.4.1 |
| 内核与用户 | C11 freestanding，i686，不使用浮点/SIMD |
| QEMU | 10.0.13，TCG，qemu32，32 MiB，单 CPU |
| GDB | 16.3，回环 stub，硬件断点 |
| 宿主内存与 capability 测试 | ASan + UBSan |

沿用 `/workspace/.liberums-tools/bin` 中已有工具，无新增第三方依赖。

## 命令和结果

在仓库根目录实际执行：

```sh
export PATH=/workspace/.liberums-tools/bin:$PATH
make clean
make -j2
make test
make
git diff --check
```

命令全部成功。第二次 make 无需重建；完整构建与验收输出保留为 `build/build.log`、`build/test.log`。
测试脚本不跳过缺失工具，GDB 非零、超时、错误退出码或断言失败都会使总验收失败。

| 套件 | 数量及结果 |
| --- | --- |
| 原宿主 memory_tests | 10,890 条断言通过，ASan/UBSan 无报告 |
| 新 capability_tests | 15 条断言通过，完整代数/实例/权限边界 |
| Phase 1 | 原断言全部通过，2 个 guest |
| Phase 2 | 原断言全部通过，22 个 guest |
| Phase 3 | 原断言全部通过，17 个 guest |
| Phase 4 | 新增 10 个 guest，各完成两轮完整生命周期，全部通过 |
| 合计 | 10,905 条宿主断言；51 个 QEMU guest；无必需项跳过 |

Phase 1/2/3 的测试文件、原 user/program.c、单页链接断言及基础六页模型均未放宽。
普通启动仍运行原正常用户域，不自动触发资源攻击。

## Phase 4 十种模式

| resource-test= | 实际验证 |
| --- | --- |
| basic | claim、query PFN/rights、指定 PFN 重取、用户选 VA、多别名、R/W、伪造/跨域/旧实例句柄拒绝、配额/别名上限、反复复用 |
| transfer | 双别名撤销、权限缩减、新代数、整页清零、接收者配额不足/收件槽满/非法目标/权限失败的原状态保持 |
| unmap-fault | 真实写读预热后仅撤销一个别名，另一个仍可读，旧 VA 的硬件 #PF |
| release-fault | 两个别名均撤销，物理页清零归还，旧 VA 的硬件 #PF |
| readonly | 缩减授权拒绝写映射，实际写只读 PTE 得到 #PF |
| transfer-fault | 旧所有者转移后访问旧别名 #PF；其故障清理不释放 B 已拥有的页 |
| exit | 用户故意带着有效资源/绑定退出，退出清理回收全部动态资源 |
| generation | FREE 槽注入 UINT32_MAX-1，实际 claim 返回完整 UINT32_MAX，转移拒绝溢出，释放后永久跳过槽 |
| map-failure | 只留一页可用，claim 成功、动态页表申请 ENOMEM，所有权仍有效且没有半个绑定 |
| claim-failure | 暂存全部剩余页，claim 返回 ENOMEM/EDX=0，没有消耗资源槽、代数或配额 |

basic 通过 GDB 把已知 A 句柄/PFN 整数写入 B 的私有测试输入；第二轮还给 A 上一域实例的旧句柄。
这些是敌对输入，不改 PTE、授权或异常路径。generation 仅对 FREE 槽注入边界值，
没有把可设置代数的接口暴露给用户。建议用 `python3 tests/phase4.py --mode ...` 完整复现，
不能仅凭单独启动测试映像的完成字符串判定通过。

## 授权拒绝和失败路径

用户程序实际检查返回码，错误就用非零状态退出；GDB 再检查双方终态与退出值。
检查覆盖：

- B 使用完整 A 句柄执行 query/map 被 EPERM 拒绝；请求 A 的 PFN 为 EBUSY。
- 映射内核地址、固定代码、未对齐或窗口外 VA 为 EINVAL；不能覆盖现有 PTE。
- 请求固件 PFN 0、内核 PFN 0x100 为 EPERM，越界 PFN 为 EINVAL。
- 索引 0/999/0xdeadbeef、代数 0/错误代数、释放后句柄、复用槽的旧代数均 ESTALE。
- 下一域实例拿到上一轮句柄也为 ESTALE。
- 每域四页和每资源四别名的上限返回 ENOSPC；重复 unmap 为 ENOENT，重复 release 为 ESTALE。
- 缺少 MAP/RELEASE/TRANSFER 权限不能执行对应操作；转移不能赋予自己没有的 WRITE 权限。
- 目标不存在/终止、自转移、未知权限、目标配额满和通知满均在修改 PTE/所有权前拒绝。

拒绝不是靠日志证明：GDB 审计资源表、计数、物理状态、原数据和双方 PTE。

## 实际所有权、PTE、代数证据

以下为 transfer 第一轮的 GDB 输出摘录：

```text
RESOURCE: 1 owner=1 epoch=1 gen=1 PFN=0x139 rights=0x3f refs=2
PTE: 1 0x09003000 0x00139000 0x00139067
PTE: 1 0x09005000 0x00139000 0x00139007

# 接收方配额不足，转移失败后：
RESOURCE: 1 owner=1 epoch=1 gen=1 PFN=0x139 rights=0x3f refs=2

# 转移提交之后，A 的动态表已撤销：
RESOURCE: 1 owner=2 epoch=1 gen=2 PFN=0x139 rights=0x2d refs=0

# B 领取新句柄并映射：
RESOURCE: 1 owner=2 epoch=1 gen=2 PFN=0x139 rights=0x2d refs=1
PTE: 2 0x09003000 0x00139000 0x00139025
```

PTE 最低三位分别为 7（用户可写）和 5（用户只读）；其余低位包含 CPU 的 accessed/dirty。
测试确认该物理页的 1024 个字全部从非零秘密模式变为零，B 不会读到 A 的旧内容。
数值随链接布局/分配顺序变化，不是固定 ABI。

每个用户检查点验证实际 CS=0x1b、SS=0x23、CR3 为当前域页目录。
GDB 读取所有 OWNED 行及全部动态 PTE，双向核对：

- 每个活跃 PFN 唯一，物理状态为 ALLOCATED，不与两域基础页或动态页表混用。
- owner_epoch 与当前域实例一致，resource_count 与表中实际数量相等。
- references 等于实际绑定数；每条 PTE 必须有本域授权记录，记录也必须有相应 PTE。
- 所有 VA 都在窗口内，所有权限均为授权子集；未指定条目为零。

## 实际硬件异常证据

GDB 在 `domain_fault` 核对真实跨级帧、EIP、向量、错误码及 CR2，不增加 EIP 或恢复故障指令。

| 实验 | 实际 EIP | CR2 | #PF error |
| --- | --- | --- | --- |
| 单别名撤销 | 0x0800058f | 0x09003000 | 4，user/not-present/read |
| 释放后访问 | 0x0800058f | 0x09003000 | 4 |
| 只读页写入 | 0x08000596 | 0x09003000 | 7，user/protection/write |
| 转移后旧别名 | 0x080006a8 | 0x09005000 | 4 |

单别名撤销时页表仍存在，第二个别名 PTE=0x00139027 仍有效，排除了只靠释放整个页表的验证。
A 故障后，B 照常申请/映射/访问/释放；转移故障实验中 B 继续领取属于自己的授权并读到整页零。
GDB 检查 A 为 FAULTED，B 为 EXITED/status=0，第二轮也一致。

## 清理和一致性

GDB 在回收点确认当前为内核 CR3、current_domain 已清除、两域 resource_count/inbox 生命周期结束。
所有资源槽为 FREE、owner 清除、引用为零；曾使用的物理资源页全部为 FREE 且整页为零。
动态页表不存在，原十二个基础页也按 Phase 3 方式清零/释放，空闲总数恢复创建前快照。

第二轮重新申请、特定 PFN 重取及连续 16 次句柄复用检查均成功；代数持续前进，不在域重建时重置。
边界实验中 UINT32_MAX 槽保持 FREE 但永久不可复用，下一 claim 从其他槽取得新资源。
当前阶段没有已知的所有权绕过、未解释 Panic 或动态页泄漏。

## 开发中失败与修正

- 初次把全部用户资源实验链接到原单页映像时，链接器正确拒绝超出 4096 字节。
  改为三组独立测试映像，未修改单页断言、六页基础或旧测试。
- 最初的资源对象通配规则也匹配了 resource_entry/resource_faults，造成重复定义和缺失入口。
  将变体限定为 BASIC/TRANSFER/FAULT 三个明确目标后构建通过。
- 两篇指定 MIT 参考原始 URL 的实际下载被代理 403 拒绝；文档说明参考范围，不伪称已阅读全文。
- 最终完整清理重建、全部旧测试和新测试已通过；上述失败没有被标记为成功。

## 日志、限制与发布

`build/phase4-*-gdb.log` 保留授权/PTE/故障审计，`*-serial.log` 保留各 guest 输出。
测试取得证据后终止自己的进程组；日志尾部 SIGTERM/Terminated 是清理，不是将超时视为通过。

限制仍然是：单 CPU、合作式执行、低 64 MiB；16 个资源槽、每域四页、每资源四别名、单通知槽。
没有 SMP shootdown、DMA 授权、异步撤销、CPU 抢占、共享资源、通用 ELF 加载器、LibOS 或 NX/完整 W^X。
没有执行真实硬件测试；同步资源操作无法强制打断无限循环域。

测试完成后按任务要求提交、推送并核对 origin/main；远程基准变化时改用分支/PR，不 force。
最终报告提供实际推送分支和提交 SHA，不将 SHA 写入自身以免循环。Phase 4 完成后停止。
