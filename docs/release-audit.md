# Phase 10 发布范围审查

开发前 HEAD/远程 main 为 Phase 9 `2a66612b2823fe7b010257373b23f9aa38252eb0`，work 分支干净。
先阅读/核对核心模块、Makefile、六个 ABI 头、用户 fixture、测试和阶段文档，再运行原完整回归。
没有更改 kernel/、arch/、include/abi/、include/kernel/ 或旧 user/ 实现。

| 类别 | 教学发布可以说什么 | 不能扩大成什么 |
|---|---|---|
| 启动/内存 | i686 Multiboot、保留页、私有页表、CR0.WP、受控映射 | 任意硬件/UEFI、任意 VM 布局、NX/完整 W^X |
| 多域 | 最多四域，私有 CR3/栈、ID/Epoch/Generation 生命周期 | POSIX、用户 Spawn、SMP、一般进程服务 |
| CPU | 可选抢占、真实 IRQ0、寄存器恢复；合作兼容 | 合作模式强制收回、实时墙钟份额或复杂调度策略 |
| 撤销 | 原协议、主动 ACK、强制 unmap/zero/free、旧指针 #PF/安全退出 | 自动页迁移、透明恢复、通用生产内存压力策略 |
| 设备 | 独占 VGA/PS2，Ring3 文本/编辑策略，实际模拟 IRQ1 | GUI、多 Console 共享、任意端口 I/O、通用驱动框架 |
| LibOS | Kernel ABI SDK、中立示例、不同 allocator/API 并存 | Aelthuvor 是唯一实现，或它的 API 是内核 syscall |

本次新增 SDK/生成器/包装工具在宿主运行；新 runtime/pool/console 在 Ring 3。
示例 Console 是本仓库 user/console/core 的教学适配，保留 GPL 与固定来源；没有复制 Ael Framework。
Aelthuvor Independent Reference 保留为外部比较/兼容对象，新 bitmap 策略独立编写。

旧 Makefile 的历史测试和入口保留。新工具检查、SDK/示例目标是兼容增加；ABI 1.2、26 调用、Note 与错误语义不变。
旧测试依赖内部 fixture/符号是有意的硬件审计，不应成为第三方构建依赖。
新消费者 .d、link map、ELF 符号与仓库外实际执行专门验证没有私有/外部隐式依赖。

历史时效由 [索引](history.md) 解释，不重写旧结论。[源码与许可](../LICENSE)、
[当前限制](known-limitations.md)、[验收与交付](release-validation.md)。
