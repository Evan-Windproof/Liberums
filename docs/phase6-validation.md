# Phase 6 实际验收记录

## 环境与开发基准

日期：2026-11-15（UTC）。工作目录 `/workspace/Liberums`，仅修改本仓库。
起始工作区干净；fetch 后从旧 Phase 5 提交快进到 main 的
`49d5dbf2e78f7ad7ed6ec4acdeece68b1b6568d0`，未重写前五阶段。
开发前完整运行原 `make test`，103 个 QEMU 场景、10,905 条原宿主断言及 49 个 ELF 解析输入通过。

Debian 13 / x86_64：GCC 14.2.0、Binutils 2.44、Make 4.4.1、Python 3、
QEMU 10.0.13、GDB 16.3（含 Python）。guest 为 pc/TCG/qemu32、i686、32 MiB、单 CPU。
QEMU/GDB 使用仓库外已经准备的 `/workspace/.liberums-tools/bin`，没有新增第三方依赖。
未测试真实硬件、KVM、多核或新任务恢复后的环境。

## 复现命令

```sh
export PATH=/workspace/.liberums-tools/bin:$PATH  # 仅当前云环境需要
make clean
make -j2
make test
make
git diff --check
python3 tests/phase6.py
make test-phase6-native
```

`make test` 先执行原 Phase 1～5，再执行 Phase 6；没有删除或放宽旧断言。
`python3 tests/phase6.py` 可单独调用，会构建所需内核/fixture 并执行两组真实 C 边界测试。
`make test-phase6` 是其 Make 入口。正常启动、预期用户 #PF 和加载拒绝各用独立 QEMU 实例。

## 最终套件结果

| 检查 | 实际结果 |
| --- | --- |
| clean、-j2 构建、后续 make | 通过；freestanding ELF32，无未定义符号，后续 make 无需重建 |
| Phase 1～4 回归 | 51 个 QEMU 场景全部通过 |
| Phase 5 回归 | 52 个 QEMU 场景及 49 个宿主 ELF 解析输入全部通过 |
| Phase 6 | 10 个 QEMU/GDB 场景，20 个创建/运行/回收轮次通过 |
| 原宿主断言 | memory=10,890、capability=15，ASan/UBSan 通过 |
| 新宿主断言 | timer=1,008、revoke=1,307，ASan/UBSan 通过 |
| 总计（唯一套件项目） | 113 个 QEMU 场景，13,220 条宿主断言，另 49 个 ELF 输入 |
| 单独 Phase 6 调用 | 再次通过相同 10 场景；不计入上行的唯一项目数 |
| 检查点稳定性追加实验 | 修正 GDB 重复停点后连续 50 个 exit-pending guest / 100 轮通过 |
| git diff --check | 通过 |

“轮次”指一个 guest 中的域槽生命周期；每 guest 两轮。ELF 拒绝场景不创建 A 的地址空间，
仍实际运行 B。没有把拒绝的 A 计成已执行的用户程序，也没有把重复实验增加为不同测试项目。

## 十个 Phase 6 场景与硬件证据

1. **spin**：A 在汇编无限循环中没有 int/yield。IRQ0 入口的真实 CS=8、IF=0；
   CPU 外层帧 CS=0x1b、SS=0x23、保存 IF=1、IOPL=0，当前 CR3 为 A 私有页目录。
   IRQ 栈地址等于 TSS.ESP0 减 CPU 外层帧长度，随后保存的 GP 与入口实际 GP 一致。
   恢复时逐项比对七个 GP 哨兵、EIP、ESP、CS/SS、DF/CF 和段状态。
   原始 DS=3，ES=0x20、FS=0x21、GS=0x18；IRET 将 null DS 归一为 0，非空段含 RPL 精确恢复。
   B 在 CPL3 的检查点运行并调用 exit(0)。A 每轮确实发生 20 次硬件抢占、yields=0，
   由显式诊断 CPU 预算终止。两轮空闲页完全恢复。
2. **ack**：A 真实申请、写入整页并建立两个别名。内核发布真实事件，A 用 24/25 读取字段，
   错代数、错请求号、pending map/transfer 被拒绝；A unmap 一个别名并 ACK，内核移除剩余别名。
   GDB 在完成处检查 FREE、引用零、PDE/PTE 清除、数据和动态表清零、事件消失。
3. **force**：A 建立两个可写别名后只在写循环中运行，不再发任何系统调用。
   请求延迟为六个交付 tick。到期在可信调度栈、内核 CR3 下完成回收；全部别名和动态 PDE 清零。
   B 对同一 PFN 重新 claim，代数增加，读取 1024 个零字后写入自己的模式。
   A 恢复访问旧 VA 时真实 #PF，error=6，CR2 为 0x09000000 或 0x09001000，
   EIP 必须是对应的写指令；不会跳过指令。B 之后再次检查整页模式未被破坏。
4. **release**：pending 时正常释放，事件取消；随后旧 ACK 为 ESTALE。B 仍取得同一已清零 PFN。
5. **exit-pending**：A 带活跃资源和事件 exit(0)，终止清理取消通知、撤别名、清零；B 正常复用并退出。
6. **immutable-abi11**：实际 ABI 1.1 SDK 编译的旧 ELF，按固定 SHA256 校验，原指令不重编译；
   默认 IF=0、私有 CR3、多页/BSS、旧资源 API、同伴和退出回收全部通过。
7. **cooperative-note**：仅在 build/ 副本修改上项 ELF 的 required flags 为 31，
   默认模式接受并保持原合作语义；代码字节和归档旧 ELF 不变。
8. **cooperative-note-rejected**：同一副本在显式抢占模式被 EPROTONOSUPPORT 拒绝，未分配 A 空间，B 正常。
9. **profile-rejection**：要求 1.2 PREEMPTION/REVOCATION 的新外部 ELF 在默认模式安全拒绝，无回滚泄漏。
10. **external-preempt**：同一个独立 ELF 在抢占模式以 CPL3/私有 CR3 运行；真实查询 version=0x10002、
    flags=119，执行不含 yield 的两千万次计算。GDB 观察实际多次 IRQ/恢复、B 完成退出、资源内容保持、
    A 正常 exit(0) 与完整回收。抢占次数随 TCG/GDB 速度变化，不固定为某个成功字符串。

内核来源 IRQ 使用显式 `sti; hlt; cli`，在域创建前测试。GDB 对比实际 GP、段、ESP、算术标志，
确认 IRQ 返回继续运行，没有切换域/CR3。PIC monitor 核对 master IMR=fe、base=20、ISR=00，
slave IMR=ff、base=28；eoi_count == irq_count，连续 IRQ 证明 EOI 未遗漏。

## 协议边界与失败回滚

`revoke_tests.c` 执行真实 resource.c/revoke.c/capability.c/tick.c；假物理页和 PTE 用于宿主测试，
不冒充真实 MMU。覆盖：错 owner/epoch、错误完整代数（含高 16 位）、伪造/重复/过期 ACK、
请求 ID=UINT32_MAX 的正确交付、ID 耗尽不回绕、重复请求、队满不覆盖、无效原因、零延迟、
deadline 溢出、恰在 deadline 的 ACK、正常释放竞争、转移后旧 ACK、终止与 expiry 同时出现、
分配 ENOMEM、绑定失败无副作用，以及 200 次重复生命周期和全部物理页归还。

`timer_tests.c` 覆盖 tick 初值、MAX 边界、延迟为零、checked addition 和 1000 个期限样本。
原 Phase 4 的真实绑定分配失败/代数/别名测试和 Phase 5 的部分 ELF 构造 ENOMEM 回滚继续通过。
耗尽 497 天的真实长跑没有执行：通过纯计数边界测试与调度路径审查验证，不标记为长期硬件实测。

## 开发中发现的问题与修正

- 初版选择子检查只接受 RPL3，审查发现合法 RPL0/1 的用户段会错误触发内核 Panic。
  改为验证 GDT 描述符身份并保留 RPL，加入上述 null DS、ES=20、FS=21、GS=18 的真实中断/恢复验证。
- GDB 硬件断点的 RF 和跨级 IRET 的 null 选择子归一不能按“所有位不变”断言。
  测试按内核公开的安全 EFLAGS 掩码核对 RF 清除，按实际 IRET 语义核对 null DS=0；其他寄存器仍精确比对。
- 两处别名写入之间可收到 IRQ，因此旧访问 #PF 可以落在任一别名。测试逐一匹配 CR2 与真实指令 EIP。
- 初版 GDB 复用硬件调试槽时偶发重复停在第一轮 reaper，曾导致单独套件失败。
  追加内核 phase6_rounds 断言，真实重现 `actual round=1`，不是第二轮资源丢失。
  现在先单步执行空检查点的 RET，再重新布置断点，并要求实际轮次递增；未删除资源断言。
  修正后连续 50 guest/100 轮及最终完整/单独套件通过。

## 兼容性证据与未执行项目

本环境没有已经构建的旧 Aelthuvor 产物，也没有访问或修改其工作区；未运行会构建该仓库的 `make test-libos`。
按任务允许的替代路径，归档 `tests/fixtures/compat/abi11.elf`，manifest 记录旧 main、SDK 三文件哈希、
构建方式和 ELF SHA256；它是兼容测试程序，**不是 Aelthuvor LibOS**。
SDK 1.1 未被悄悄替换为 1.2。Aelthuvor 实际旧产物的只读重跑仍可另行进行，不计为本次通过项。
历史 `tests/libos.py` 的精确 version=1.1/SDK 当前工作目录相等断言未重写；未来升级该集成工具需独立审查。

已知范围：单 CPU/两个域，PIT tick 不是墙钟，内核不抢占；无硬实时延迟保证，计数耗尽关闭执行。
默认合作模式仍允许不合作用户独占 CPU，必须显式选择抢占 profile 才有强制 CPU 控制保证。
仅撤销普通资源数据页；无 DMA/设备撤销、SMP/TLB shootdown、线程、信号或异步回调。
没有自动内存压力选择策略；`phase6=` 中的压力源和 20 次 CPU 预算仅供诊断。
没有实现 NX/完整 W^X、POSIX、文件系统或新的 LibOS。到此停止，不进入 Phase 7。

运行日志保留在 `build/phase6-*-gdb.log`、`*-serial.log`、生成的 `*.gdb`；完整命令输出为
`/tmp/phase6-final-{clean,build,test,make,diff-check}.log`、`/tmp/phase6-standalone.log`、
`/tmp/phase6-native-final.log` 和 `/tmp/phase6-checkpoint-stability.log`，均不提交。
GitHub 提交/远程 SHA 由交付报告核对；本文不预先写入尚未生成的提交号。
