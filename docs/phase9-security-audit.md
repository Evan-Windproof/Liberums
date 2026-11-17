# Phase 9：安全审计清单

审查依据是已声明的 TCB：可信内核、boot 配置/加载器与 QEMU/硬件；用户域及 LibOS 不可信。
不是对所有硬件、侧信道、恶意加载器或无限运行时的形式化安全证明。

## 按严重程度记录

| 编号 | 分类 | 发现、处置与证据 |
|---|---|---|
| S9-01 | P2，已修复 | KBD_RELEASE 错误时 EDX 原样保留，如 `0xcafe1234`，违反原公开头文件的零输出声明；在实际 CPL3 复现后增加一行清零。原 errno 不变，修复后 EDX=0；反例与通过日志都保留 |
| S9-02 | P1 验证缺口，已补齐；非已证实内核漏洞 | 过去 native/内置程序/设备测试不能证明外部 LibOS 普通 RAM 撤销；新增两种 Ael 分配策略、真实协议 trace、两别名失效、清零、旧句柄拒绝、安全退出/#PF和另一 LibOS 存活 |
| S9-03 | P2，已明确的兼容限制 | TRANSFER 目标仅 ID，复用后可主动送到当前新 Epoch；对抗实验复现正确路由。没有假称旧调用具有目标 Epoch 保证，未改变 ABI |
| S9-04 | P2，诊断加固 | 新诊断 mailbox 显式验证目标 Epoch、消费单次提交；默认关闭。缺域、旧 Epoch、旧/他域句柄和零延迟都安全失败，不改权限状态 |

本次没有发现可复现的 P0 跨域越权、内核内存破坏或所有权保护失效，
也没有以虚构的漏洞修复替代测试。没有留待后续修复的已知 P0/P1 实现缺陷；下列架构限制仍存在。

## 审查与验证映射

| 边界 | 审查结论／实际检查 |
|---|---|
| trap 身份 | 域 RUNNING、CS=1b、当前 CR3、来源 frame 位于专属内核栈；TSS ESP0 每次切换。用户不能传入 kernel domain 指针 |
| ID/Epoch/Generation | 槽位清理后才复用；资源 owner 双身份，代数完整 32 位，MAX 不回绕；旧权限、inbox、事件不能继承 |
| transfer | 所有拒绝在提交前；验证目标活跃、权限子集、配额、inbox、代数；移除原所有别名、清零后更新 owner/代数；RECEIVE 不创造资源 |
| revocation | 请求只有可信侧；事件绑定 owner ID/Epoch+handle+request；ACK 时间检查，deadline 到期强制完成；RELEASE/退出取消；不重复释放 |
| RAM/MMU | 普通页仅来自 FREE RAM；模块、内核、页表、VGA 不可 CLAIM。MAP 只允许 resource window、READ[WRITE] 权限子集；删除活动别名会失效 TLB，非活动域重载 CR3 |
| ELF | 原有长度/溢出/重叠/权限/入口/Note 校验；每域独立文件/BSS/栈；部分 OOM unwind、保留其他域。旧恶意 corpus 全部保留 |
| 调度 | 单 CPU/IF=0 事务；IRQ0 保存全部 GP/段/CPU frame，恰好 EOI；READY 才运行，终止后不再调度；128 轮 EXIT/#UD 交替不破坏另外三域 |
| VGA/Keyboard | 显式 ID/Epoch grant、独占句柄、代数、MMIO 与 RAM 分流、IRQ1 FIFO/溢出、EOI、退出/Fault/复用/失败 grant 回滚；原硬件及 native 覆盖继续执行 |
| syscall | 仅标量，不解引用用户指针；不认识的调用 ENOSYS。EDX 只按明确契约解释；KBD_RELEASE 既有声明已兑现 |

## 未改变的限制

单 CPU、内核不可抢占；合作 profile 无强制 CPU 收回保证。容量保持有界。
普通 i686 分页没有 NX，RX/R/RW 不是硬件完整 W^X；FPU/SIMD 仍按原设计禁止。
强制撤销不能恢复旧指针；Aelthuvor 活跃或丢失页采取退出 96，旧指针仍可能 #PF。
设备独占，无共享服务或多 Console 同屏；VGA 按页映射，4000 字节屏幕后的 96 字节仍是同页设备 padding。
ID-only transfer 不提供缓存目标身份稳定性。128 轮是有限压力测试，不是数小时或数十亿次 Epoch 长跑。
