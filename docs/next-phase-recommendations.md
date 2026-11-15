# 下一阶段建议：优先 Aelthuvor Phase 2

**推荐方案 B：先规划并开发 Aelthuvor Phase 2。** 这只是审查结论，本次没有开始该阶段，也没有开发 Liberums Phase 7。

依据是当前最明确的缺口在独立 LibOS 对现有内核机制的适配：本次未修改的 ABI 1.1 Aelthuvor 已在 ABI 1.2 合作 profile 完成 First/Best-fit、映射、访问、释放、故障隔离和回收；但是原集成入口失败，原 ELF 的 COOPERATIVE 要求又使其不能在 preempt profile 运行。内核已有抢占和页撤销证据，继续扩大内核功能之前，应先用独立用户态策略检验其契约是否足够。

详细执行状态与源码依据分别见 [集成报告](aelthuvor-integration-audit.md) 和 [架构报告](exokernel-architecture-audit.md)。以下 Phase 7/2 内容是**建议候选范围**，不是假定仓库已定义或承诺的 roadmap。

## 两个方案比较

| 角度 | 方案 A：Liberums Phase 7 | 方案 B：Aelthuvor Phase 2 |
|---|---|---|
| 架构必要性 | CPU capability、用户 fault/event dispatch、通用地址布局等有价值，但当前未证明都必须先做 | 先消费已有公开接口，直接检验保护机制是否允许独立 management；与现有证据衔接更强 |
| 当前依赖 | 若先扩 SMP、设备或 VM，需同步/每 CPU 状态/TLB shootdown 等新前提，复杂度大 | 可从 SDK、测试入口及执行 profile 契约着手；1.2 cooperative 已可运行，具备可持续比较的基线 |
| 安全性 | 新特权代码扩大可信计算基；需补上新的故障/竞争矩阵 | 多数策略改动在 Ring 3，风险局限较好；但处理 live allocations 与异步失去页时仍有高难度正确性问题 |
| 实际开发难度 | 中至高，取决于范围；SMP/设备隔离明显高于现有单核模型 | SDK/旧行为回归较低；抢占 profile 较中等；可靠撤销恢复高，不能把加三个 syscall wrapper 当完成 |
| 长期维护价值 | 通用内核事件/完成记录、明确 abort 契约有长期价值；不宜先堆无消费者的接口 | 建立跨版本测试、固定 SDK 更新流程、多个分配策略和协议状态机，能降低后续每次内核升级成本 |
| 对 Exokernel 理念的验证 | 增加低层机制证明能力宽度；单靠内建 fixture 容易继续自洽 | 用独立 ELF、独立 SDK、独立算法选择验证内核无需理解 LibOS metadata，最直接验证 Protection / Management 分离 |

## 建议的 Phase 2 验收顺序

1. **先修复版本与测试维护契约。** 保留 vendored SDK SHA256/文件集/精确来源校验；将“当前内核头必须与旧 SDK 完全相同”从一般运行兼容测试中拆出，保留为 SDK 更新来源检查。运行测试按 major/minor/features 验证。修正历史 GDB 的固定 `0x10001`，继续保留 ABI 1.1 immutable ELF 回归。这些是未来需要在两个仓库分别审核的维护任务，本次只新增独立审计脚本并记录原失败。
2. **声明执行 profile。** 合作构建继续要求 COOPERATIVE。新增支持抢占的构建只能在代码与测试准备好后修改 ELF note 和 runtime required features，不能把删除 bit8 当作已支持抢占。跨域单线程暂停/恢复不必自动给 heap 加锁；同域线程、upcall、重入须另行定义同步和安全点。
3. **设计可撤销页与 live block 的语义。** BSS 中的 pool/arena metadata、动态 payload、应用 raw pointer 是三种不同生命周期。定义 pending、主动归还、ACK、强制失去、ESTALE 与重复事件。空 arena 可优先 trim；有 live blocks 时选择迁移/保留 VA 重映射、拒绝/延迟（如协议允许）或明确受控失败，不能默默让 alloc 返回失映射地址。元数据本身及处理事件所需栈/代码必须可继续运行。
4. **先确定强制完成可见性，再承诺恢复。** 当前 `kernel/revoke.c:104` 删除 active 事件后，晚轮询看不到已失去的页。若 Phase 2 需要超时后仍能恢复，应与内核约定通用 completion/tombstone、通知或受控终止方案。可能需要一个单独审查的小型内核协议补充；不应让内核调用 Aelthuvor arena 函数。协议未解决前只声明可验证的失败隔离，不能声明透明恢复。
5. **以真实 ELF 做双向验证。** First/Best 都覆盖合作旧行为、抢占中应用长循环与 peer 进度、空页与活页撤销、旧 ACK/旧 handle、release/transfer/exit 交错、元数据安全点及反复回收。记录实际 CPL3、CR3、PTE/TLB、物理清零与 source/SDK/ELF hash，区分 loader 拒绝、runtime 拒绝与成功执行。不同 LibOS 并发应作为独立验收，不能拿两个算法变体冒充。

## 哪些情况应改变优先级

若以后复现当前内核的跨域越权、错误 owner 释放、旧 ACK 收回新 owner 页、TLB stale translation 或上下文恢复破坏，应先修复并回归这些内核安全问题，再推进 LibOS 功能。如果目标变为 SMP、用户可恢复缺页或直接设备资源暴露，则要先明确内核新增机制与安全前提，可能适合以受限 Phase 7 为先。本次没有实测发现这种必须立即重写内核的阻塞。

当前更合理的推进方式是**先验证独立 LibOS 对现有契约的真实适配，再由具体缺口驱动最小通用内核改动**。这既保留已验证的 Exokernel 分层，也避免在内核中实现用户堆策略。本次工作在审查文档安全提交、推送及远程 SHA 核对后结束。
