# Liberums Exokernel 架构规划

本文描述长期 Exokernel 边界与当前实现。Phase 6 在外部 ELF/资源保护上增加公开 ABI 1.2、
显式 PIT 抢占模式与普通资源页的可见撤销，见 [Phase 6 概览](phase6-overview.md)。
默认合作兼容模式仍支持旧 ELF，不链接或修改 Aelthuvor。当前无 SMP、通用设备/DMA 撤销或完整 LibOS。
Phase 7.1 仅增加固定 VGA 文本页授权、同步解除映射与用户态 Console，
见 [VGA 设备边界与验收](phase7-vga-console.md)；普通 RAM 与设备生命周期分开。
Phase 7.2 增加 PS/2 IRQ1 的有界授权扫描码队列；字符、编辑与小型命令 Console 仍在 Ring 3，
见 [键盘输入与交互 Console](phase7-keyboard-console.md)。不提供通用输入框架或完整 Shell。
Phase 7.3 将相同用户策略以独立 ELF 启动，通过显式可信配置发出设备 grant，
不把 ELF note 当授权；见 [外部 Console](phase73-overview.md) 与 [边界审查](exokernel-boundary-review.md)。
Phase 8 在保留旧单 ELF/peer profile 上，增加有界动态四槽域池和多 Multiboot ELF profile。
使用既有 CR3/TSS、抢占、Capability 与独占设备机制；完整清理后才发布可复用槽位，Epoch 不回绕。
不新增用户态进程服务或 LibOS 策略，见 [动态域](phase8-overview.md)、
[模块关系](phase8-modules.md) 与 [Phase 8 边界审查](phase8-boundary-review.md)。
Phase 9 保持 ABI 1.2 和容量，审查身份/转移/撤销/设备/调度并补齐外部 LibOS 普通 RAM 撤销 E2E。
新增的可信诊断只在显式 selftest 下提交带预期 Epoch 的撤销请求；无用户撤销能力、应用名称或私有结构耦合。
唯一生产 syscall 修正是 KBD_RELEASE 的既有 EDX 零输出契约。
见 [安全审查](phase9-security-audit.md)、[ABI 契约](phase9-abi-contract.md)、
[不同 LibOS 与中立教学路线](phase9-libos-compatibility.md) 和 [新增特权边界](phase9-boundary-review.md)。

## 目标与安全模型

目标是让互不信任的用户域运行自己的 LibOS，直接选择资源管理策略，
同时由小型内核仲裁资源访问并防止跨域破坏。
参考 MIT Exokernel 的机制/策略分离思想，不复制其或其他 OS 的内核实现。

可信计算基包括内核、引导加载器和实际使用的硬件/模拟器。
LibOS 和应用均不可信，不能自行修改页表、提升权限、执行任意端口 I/O，
也不能直接获得任意物理内存或 DMA 能力。
初期目标为单核 i686；多核、硬件虚拟化和任意物理设备透传不在初始范围内。

## Protection 与 Management 的边界

| 资源/功能 | 内核：保护机制 | 用户态 LibOS：管理策略 |
| --- | --- | --- |
| 物理内存 | 识别可用页、保留内核/固件/引导区、记录页所有者、检查映射权限、跨所有者交付前清零 | 选择申请哪些页、虚拟地址布局、堆/栈、分配器、缓存、回收优先级 |
| 地址空间 | 受控页表更新、隔离 ring 0 与 ring 3、验证地址/溢出/权限、TLB 失效 | 进程模型、内存区域语义、按需调页策略 |
| CPU | 上下文隔离、时钟中断、可强制收回的执行时间、最低公平性与配额约束 | 在获得的时间内调度用户线程、协程、应用优先级 |
| 异常与事件 | 安全保存上下文、交付事件、验证恢复状态、防止异常影响其他域 | 用户异常处理、兼容信号或其他运行时语义 |
| 块存储（未来） | 校验块区间所有权、请求边界及设备安全访问 | 文件名、目录、文件系统格式、日志、块布局与缓存 |
| 网络（未来） | 校验队列/缓冲区和发送权限、接收流分派、避免 DMA 越界 | TCP/IP、拥塞控制、socket 接口与缓冲策略 |
| 设备 | 必须留在可信侧的寄存器验证、资源边界和最小驱动机制 | 设备使用顺序、批处理、吞吐/延迟取舍及高层驱动策略 |
| API | 小而显式、可检查的资源操作接口 | POSIX 或其他应用 API，不作为内核固有抽象 |

内核记录空闲页或轮转运行域所需的最小数据结构是实现保护的必要机制；
它不应演变成内核内复杂堆分配器、文件系统或完整进程/线程管理策略。
必要的安全仲裁规则必须公开，不能以“策略全部在用户态”为由放弃配额和防饥饿约束。

## Resource Ownership

后续每种可分配资源都有内核维护的身份、所有者域、权限集合、代数和状态。
最初可采用单所有者模型，显式共享再引入受限授权。

应始终成立的约束：

- 除显式授权的共享外，同一物理页不能同时交给两个互不信任的域。
- 用户不能映射内核、页表、固件、其他域或超出实际可用内存的物理页。
- 资源从 A 转交给 B 前，必须清除 A 的映射、完成必要的 TLB/设备同步并清零残留数据。
- 销毁域会撤销其绑定并回收资源；回收途中失败不能产生“两个有效所有者”。
- 用户提供的指针、长度、资源句柄和权限都必须经内核验证，含边界、整数溢出和生命周期。

所有权表属于内核私有内存。用户可见句柄不是物理地址，也不是可凭数值自行构造的授权。

## Secure Bindings

Secure binding 指“先验证授权，再建立后续可由硬件或廉价检查执行的安全绑定”，
不是把资源策略硬编码到内核，也不必等同于使用加密签名。

当前 Phase 4 使用全局固定资源表：用户传递索引和完整代数，内核检查调用域 ID/epoch、活跃状态和权限。
索引复用必须更新代数；在 32 位平台上应明确处理代数耗尽，不能依赖无限计数器。
委托操作只能产生权限子集，必须记录生命周期与撤销关联。
只有检查通过，内核才可建立物理页到该域地址空间的映射。
此后 MMU 执行页面级访问检查，避免每次普通访存都陷入内核。
用户永远不能通过传入页表内容映射任意物理页。

长期接口方向如下；当前 page 操作的实际寄存器 ABI 见 [Phase 4 安全绑定](phase4-secure-bindings.md)，其中本阶段事件接口采用整数轮询：

```text
page_claim()                         -> 本域的物理页句柄
page_map(page_handle, va, rights)     -> 验证所有权后建立绑定
page_unmap(va)                       -> 删除绑定并同步 TLB
resource_release(handle)             -> 安全归还资源
revoke_poll()/revoke_info(id, field) -> 查询本域待撤销事件
revoke_ack(handle, generation, id)   -> 验证完整身份后回收
```

当前 ABI 只传整数，不接收用户指针。未来事件或指针接口仍需独立的安全复制与生命周期设计。
普通 32 位分页没有通用的 NX 保证；未来不能把 ELF 权限或“只读数据页”误报为完整 W^X。
可使用 CR0.WP 与页表的 supervisor/read-write 位保护内核与只读映射，
执行权限限制需单独选择支持的硬件模式并验证。

## Visible Revocation

资源回收必须对 LibOS 可见，允许它决定保留什么、搬迁什么、丢弃什么：

```text
owned -> revoke_pending（发出事件及截止时间）
      -> 自愿解除绑定并 ack -> reclaimed
      -> 超时/拒绝 -> 内核强制解除绑定 -> reclaimed
```

Phase 6 内核事件包含资源句柄、代数、owner ID/epoch、原因、绝对 tick 期限与请求身份；ACK 匹配完整请求。
每域两个记录，队满不提交资源状态变化。PENDING 禁止新绑定/转移，允许 unmap/release。
到期在 IRQ 取得控制权后、可信调度栈上执行全别名撤除，重新激活 CR3 刷新 TLB，再清零交付。
计数与请求号不回绕，耗尽安全失败。真实机制与范围见 [撤销设计](phase6-revocation.md)。
撤销等待不能无限延长，事件通道也不能依赖正在撤销的唯一一页资源而无法工作。
LibOS 可以选择驱逐缓存、迁移数据或终止自己的任务，但不能否决内核的最终保护动作。

强制回收必须先阻止旧所有者再次提交请求，撤销全部相关映射与派生授权、同步 TLB、
处理在途 I/O，最后再清零并允许重新分配。重复/过期 ack 不能释放新所有者的资源。
如果某设备无法安全停止 DMA，资源不得在 DMA 仍可能写入时交给另一域。
初期不提供通用设备透传：没有可验证的 IOMMU/缓冲区隔离路径，就不声称设备隔离成立。

## LibOS 组织方式

LibOS 将与用户应用处于同一不可信保护域，静态链接即可形成最初的实验目标。
不同 LibOS 可使用不同内存分配器、调度器和文件系统；内核不要求 POSIX。
跨域服务属于未来显式授权协议，不让一个全能服务进程替代缺失的内核资源所有权检查。

Phase 1 的 COM1 代码是单域启动诊断设施。未来向用户态提供控制台时应设计受限能力接口，
不能赋予用户态任意 `in`/`out` 权限。诊断设施最终也应考虑输出配额与信息泄露边界。

## 下一阶段建议（只规划，不在本阶段实施）

Phase 5 已提供外部程序边界：独立的 ET_EXEC、公开的 ABI/ELF profile、私有多页映像/栈与原资源窗口。
内核只解释有界 LOAD 和 ABI note，不解释 LibOS 数据结构或管理策略，也不链接 Aelthuvor。
原单页内置路径继续保留为兼容性实验；加载失败的空间在激活前完整回滚。

未来 LibOS 应以独立用户软件使用这些接口，不把堆、缓存、文件系统等策略迁入内核。
Phase 6 已实现单 CPU IRQ0、一 tick 轮转、有界事件/ACK/超时强制回收；默认模式不改变旧用户的 IF/顺序假设。
通用 ELF、用户指针、APIC/SMP、设备撤销和更复杂的 CPU 仲裁需要独立设计。Phase 7.1 的 VGA 页实验现已单独实现，不代表通用设备框架或完整 Phase 7。

## 参考

- [Engler, Kaashoek, O'Toole: Exokernel: An Operating System Architecture for Application-Level Resource Management (SOSP 1995)](https://pdos.csail.mit.edu/papers/exokernel-sosp95.pdf)
- [Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)（中断、TSS、IRET、分页与 TLB）
- Intel 8259A / 8254 数据手册（ICW、EOI、PIT channel 0 rate generator）
- [GNU Multiboot v1 规范](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html)

参考架构思想和公开协议不代表已实现论文中的能力；每项保护机制都需要后续可执行测试。
