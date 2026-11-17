# Phase 9：新增特权逻辑与停止边界

原则仍为 **Kernel protects resources; LibOS manages resources**。

| 变更 | 为什么在可信侧／是否扩大 TCB | 是否增加公开 ABI或绑定某个 LibOS |
|---|---|---|
| syscall KBD_RELEASE 清 EDX | 返回 trap frame 是内核负责的原契约；没有新资源策略，只有一行输出修复 | 不新增 ABI；普通键盘调用者同样适用 |
| tests/phase9_runtime.c | 资源撤销请求必须由可信侧发起；只在显式 selftest 启用，属于诊断 TCB | 没有 syscall、用户指针、ELF 名称、私有元数据或应用策略；目标是 ID/Epoch/handle |
| runtime_checks 编排 | 在原安全调度返回点调用诊断；未改调度算法、CR3/TSS 或保护逻辑 | 不识别 Ael/Reference，不改变正常启动授权 |
| 外部测试 app | 不需要特权，故只在 Ring 3；只用公开 Application SDK/Kernel ABI | 单独 ELF，不链接进内核，不改外部源码 |
| ABI/边界/压力测试 | 宿主或 GDB 观察；私有 scalar 驱动合法用户操作 | 不通过修改 PTE、事件或 LibOS 私有结构制造成功 |

撤销选择只由可信测试请求确定，没有自动内存受害者策略、普通用户撤销能力或资源服务管理器。
没有修改 allocator、Console、应用 context、对象池或显示布局的责任分工。
原 grant/Capability、MMIO、IRQ0/IRQ1 和域清理机制保持。

正常 boot 未带 selftest=phase9 时新诊断不执行，即使可信调试器写入 mailbox 也不会发起请求；
该反例有真实硬件时间片测试。用户本来就无法访问 mailbox。
诊断受理旧 Epoch 会返回 ESTALE，不会在槽位复用后误撤销新拥有者。

本阶段不实现透明指针恢复、用户态 Spawn、Fork/Exec、通用 IPC、SMP、GUI、文件系统、网络、
驱动框架、动态链接或多 Console。同样不因当前 CPU 没有 NX 而声称完整硬件 W^X。
没有已知未解决的 P0/P1 实现问题；ID-only 路由、固定容量、合作模式 CPU 限制、
活跃 RAM 强制撤销后的安全失败等边界见 [安全审计](phase9-security-audit.md)。

Phase 10 只留下来源/接口/失败案例和中立学习路线建议，不提前实现全部教材。
完成 Phase 9 后停止，不创建派生发行版，不修改或推送 Aelthuvor。
