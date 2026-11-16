# Phase 8：Exokernel 边界审查

原则保持：**Kernel protects resources; LibOS manages resources.**

| 新增/修改模块 | 为什么需要可信特权 | 未放入其中的用户策略 |
|---|---|---|
| `domain_pool.c` | 原子预留身份、检查残留授权、创建 CR3、推进 Epoch、清理上下文和页表 | 没有 POSIX 进程语义、父子关系或用户 spawn API |
| `multi_external.c` | 处理可信 Multiboot 模块边界，逐个安全创建，显式设备 grant，最终回收 | 不识别 ELF 文件名、LibOS 品牌、堆策略或业务入口 |
| 原 `scheduler.c` | 仲裁 CPU；检查 READY/CR3/TSS/终止状态 | 没有应用优先级、命令处理、内存分配策略 |
| 原 `revoke.c` | 对四域维护有界保护事件与身份 | 没有自动内存压力受害者策略 |
| 只读 busy 查询 | 证明槽位没有残留资源、设备或撤销授权 | 不授予权限、不修改资源 |
| `tests/phase8_runtime.c` | 显式诊断启动中，在安全调度边界调用同一生命周期/撤销 API | 无应用名称/固定入口；正常启动不执行诊断请求 |

`main.c` 只将 boot context 传给诊断初始化；`external.c` 只对明确 multi profile 委托新启动者。
所有 ELF 继续经过原 parser；正常启动没有任何 Aelthuvor 特殊分支。
测试通过外部 ELF 实际运行，未把 Aelthuvor 链接进内核；其源码、SDK、历史和远程仓库均未修改。

创建/回收事务只在内核栈、IF=0 下运行；不是任意用户指针接口。
没有新的公开 ABI，也没有放松内核页表保护、端口 I/O、只读/地址范围或设备权限检查。
测试控制对象属于 supervisor 内核内存，只有可信调试器和显式 selftest 钩子访问。
关闭 `selftest=phase8` 时，槽位复用、压力和撤销诊断均不执行。

## 已知限制与停止范围

- 固定容量四域、八个 boot 模块、单 CPU；没有可增长域表或用户态进程管理服务。
- 启动模块只能由可信 boot 配置选择；正常 multi 启动加载后运行至全部终止，不自动补充待运行模块。
- 设备保持独占；没有多个 Console 同屏策略、合成、终端复用或 UI 管理。
- 旧 ABI 的转移目标仅 ID，见 [资源文档](phase8-resource-management.md) 的路由限制。
- 普通 i686 分页没有 NX，不能将 RX/R/RW 记录声称为硬件完整 W^X；FPU/SIMD 仍按旧约束禁止。
- 没有 fork/exec、POSIX、通用 IPC、文件系统、网络、动态链接器、完整 Shell、SMP。
- 没有实际运行数十亿次 Epoch 复用；MAX 由真实域池代码的宿主 ASan/UBSan 边界测试验证。
- 云端使用 QEMU/TCG 和真实模拟 PS/2；实体键盘、窗口焦点及本阶段本地 GTK 交互仍待维护者确认。

本阶段到 Phase 8 停止，不进入 Phase 9，不修改 Aelthuvor。
