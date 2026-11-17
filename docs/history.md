# 历史阶段索引（不移动原始资料）

旧报告保留当时基准、失败记录和证据；其中“两个域”“单外部模块”“尚未验证外部 RAM revoke”
可能是准确的历史事实，不是当前功能表。当前范围从 README、架构课程与限制说明进入。

| 阶段 | 原始入口 |
|---|---|
| 1 启动/串口 | [验证](phase1-validation.md) |
| 2 页/异常 | [概览](phase2-overview.md)、[验证](phase2-validation.md) |
| 3 用户域 | [概览](phase3-overview.md)、[验证](phase3-validation.md) |
| 4 Ownership/Bindings | [概览](phase4-overview.md)、[验证](phase4-validation.md) |
| 5 ABI/外部 ELF | [ABI](phase5-abi.md)、[加载器](phase5-external-loader.md)、[验证](phase5-validation.md) |
| 6 IRQ/撤销 | [概览](phase6-overview.md)、[撤销](phase6-revocation.md)、[验证](phase6-validation.md) |
| 7.1/7.2/7.3 | [VGA](phase7-vga-console.md)、[键盘](phase7-keyboard-console.md)、[外部 Console](phase73-overview.md) |
| 8 动态四域 | [概览](phase8-overview.md)、[生命周期](phase8-domain-lifecycle.md)、[资源](phase8-resource-management.md)、[验证](phase8-validation.md) |
| 9 稳定性 | [契约](phase9-abi-contract.md)、[审查](phase9-security-audit.md)、[兼容](phase9-libos-compatibility.md)、[验证](phase9-validation.md) |

[早期架构审计](exokernel-architecture-audit.md) 固定到其文首旧 commit；它的两域/恢复缺口描述不应成为当前宣传。
Phase 8 扩为四域，Phase 9 补齐外部普通 RAM 撤销 E2E，但仍没有透明恢复旧指针或一般压力管理策略。
本次没有删减、重写或迁移任何旧阶段报告/证据。
