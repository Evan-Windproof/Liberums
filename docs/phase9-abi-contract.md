# Phase 9：Kernel ABI 1.2 契约审查

Kernel ABI 是内核与不可信域之间的标量接口；Aelthuvor Application SDK 1.0 是用户态 API，
`ael_*` 不是内核必须实现的 syscall。Independent Reference 完全不需要该 SDK。
本阶段没有新增调用、feature 或 minor，六个公开头与 Phase 8 字节相同。

入口 `int 0x80`，EAX=调用号，B/C/D/S 分别表示 EBX/ECX/EDX/ESI。
EAX 按有符号 32 位解释：状态调用成功为 0，查询/申请可返回正值，错误为负 errno。
表中未定义的额外输出不可当新数据使用；大部分状态调用的 EDX 保留输入。
EBX/ECX/ESI/EDI/EBP 由保存 frame 保留；系统调用后段寄存器归一为用户数据段，
EFLAGS 清除 IOPL/NT/VM/调试等权限位，IF 由 profile 决定，不能依靠 syscall 保留用户 null 段。

## 全部 26 个公开调用

| 号／名字 | 输入（B/C/D/S） | EAX／EDX 输出 | 授权、状态与主要错误 |
|---|---|---|---|
| 0 YIELD | 无 | 0／保留 | 当前域 READY，切换调度；合作与抢占均可 |
| 1 EXIT | B=status | 不返回 | 仅终止调用域，清理资源；不会向用户返回 |
| 2 LOG_CHAR | B=0..255 | 0／保留 | 每域最多 128 字符；EINVAL、EAGAIN；无用户字符串指针 |
| 3 ABI_QUERY | 无 | version／features | 只读；不授予资源 |
| 4 RESOURCE_LIMITS | 无 | 4／4 | 每域资源、每资源绑定上限；不是剩余资源承诺 |
| 16 PAGE_CLAIM | B=rights | index／generation | 新 RAM 所有权、内容零；EINVAL、ENOSPC、ENOMEM |
| 17 PAGE_SPECIFIC | B=PFN,C=rights | index／generation | 仅 FREE 普通 RAM；EINVAL、EBUSY、EPERM、ENOSPC；内核/模块/MMIO 拒绝 |
| 18 PAGE_MAP | B=index,C=gen,D=VA,S=rights | status／保留 VA | 需 MAP 及 READ[WRITE] 子集；最多四绑定；STALE、EPERM、EBUSY、EINVAL、ENOSPC、ENOMEM |
| 19 PAGE_UNMAP | B=VA | status／保留 | 本域已有绑定；EINVAL、ENOENT；移除一别名，末项删除页表 |
| 20 PAGE_RELEASE | B=index,C=gen | status／保留 | 需 RELEASE；撤销全部别名、取消事件/未读 inbox、零化归还；STALE、EPERM |
| 21 PAGE_QUERY | B=index,C=gen | PFN／rights | 需 QUERY；ESTALE/EPERM 时 EDX=0；不是任意物理地址查询 |
| 22 PAGE_TRANSFER | B=index,C=gen,D=target ID,S=rights | status／保留目标 ID | 需 TRANSFER 及权限子集；目标当前活跃身份，移除别名/零化/新代数/inbox；ESRCH、EINVAL、EPERM、EBUSY、ENOSPC、EAGAIN、EOVERFLOW、ESTALE |
| 23 PAGE_RECEIVE | 无 | index／generation | 消费本域单项 inbox；已占配额，不复制页；ENOENT 或验证失败时 EDX=0 |
| 24 REVOKE_POLL | 无 | status／request | 抢占 profile；非消费式最旧事件；ENOENT/EPROTONOSUPPORT 时 EDX=0 |
| 25 REVOKE_INFO | B=request,C=field 0..3 | status／value | 本域当前 Epoch 的事件；index/gen/reason/deadline；ESTALE、EINVAL、EPROTONOSUPPORT，失败 EDX=0 |
| 26 REVOKE_ACK | B=index,C=gen,D=request | status／保留 request | 抢占 profile，完整事件身份；deadline 前回收，已到期则强制回收并 ESTALE；重复/他域/旧事件拒绝 |
| 32 VGA_INFO | B=device=1 | 80\|25<<16／4096 | 可选扩展，发现不是授权；ENOENT 或旧内核 ENOSYS；错误 EDX=0 |
| 33 VGA_ACQUIRE | B=1,C=R[W] | 0x10001／generation | 显式 grant ID/Epoch、权限子集、独占；EINVAL、EPERM、EBUSY、EOVERFLOW，错误 EDX=0 |
| 34 VGA_MAP | B=handle,C=gen,D=0x09400000,S=R[W] | status／保留 VA | 固定 MMIO，at most one binding，PCD/PWT；ESTALE、EPERM、EINVAL、EBUSY、ENOMEM |
| 35 VGA_RELEASE | B=handle,C=gen | status／保留 | 解除映射、失效句柄；不清零/释放设备页；ESTALE、EPERM；boot grant 保留到域清理 |
| 36 VGA_CURSOR | B=handle,C=gen,D=cell 0..1999 | status／保留 cell | 需 WRITE、已映射；ESTALE、EPERM、ENOENT、EINVAL、EIO |
| 40 KBD_INFO | B=1 | 64／scan set 1 | 已启用设备才可查询；旧内核 ENOSYS、不可用 ENOENT；错误 EDX=0，无授权效果 |
| 41 KBD_ACQUIRE | B=1 | 0x10002／generation | 可信 grant ID/Epoch、独占；EINVAL、EPERM、EBUSY、EOVERFLOW；错误 EDX=0 |
| 42 KBD_POLL | B=handle,C=gen | status／raw byte | 需 READ；空队列 EAGAIN；EOVERFLOW 的 EDX=丢弃数量并清批次；其他错误 EDX=0 |
| 43 KBD_RELEASE | B=handle,C=gen | status／0 | 清队列、失效句柄，仍可在 grant 内重 ACQUIRE；ESTALE、EPERM；兑现既有零输出声明 |
| 44 KBD_STATS | B=handle,C=gen | count／lost | 需 READ；丢失统计饱和；ESTALE、EPERM，错误 EDX=0 |

CLAIM/SPECIFIC/RECEIVE/ACQUIRE 在失败时不会发布新 handle，EDX=0。
VGA/Keyboard 与 RAM 是不同句柄命名空间，设备句柄不能转移为普通 RAM。
无公开 REVOKE_REQUEST，测试 mailbox 不属于此表。
错误检查顺序以实现为准：同时给出多个错误输入时，不承诺固定优先级。
错误返回不产生未声明的资源/所有权改变；ACK 在 deadline 边界属于已声明的强制完成例外。
只有 REVOKE 三调用直接要求抢占 profile；Keyboard 的可信初始化本身也要求 PIT 已启用，
因此可用键盘队列依赖抢占模式。VGA 和普通页操作可用于两类 profile。
成功 QUERY 不是 pin，随后的 IRQ0 仍可能撤销页；调用者必须接受旧指针 #PF 的边界。

## 版本、特性与格式

- version `0x00010002`；major 必须相同、kernel minor 至少为 ELF 所需 minor，所需 features 必须为子集。
- 合作 profile features=31：PAGES=1、TRANSFER=2、ELF32=4、COOPERATIVE=8、LIMITS=16，用户 IF=0。
- 抢占 profile features=119：去掉 COOPERATIVE，增加 PREEMPTION=32、REVOCATION=64，用户 IF=1。
  因此合作承诺的 ELF 不能直接放入 multi-external；原单 ELF cooperative profile 继续存在。
- 可选设备扩展用 INFO 探测，不以 minor=1.2 推断一定可用，也没有新增设备 feature 位。
- ELF32、i386、非动态链接；36 字节 note，namesz=9、descsz=12、type=0x4c414249，名称 LIBERUMS，
  major/minor/required_features 在偏移 24/28/32；原格式不变。
- handle 是两个 uint32，8 字节；Generation 不压缩，resource rights 为 READ=1、WRITE=2、MAP=4、
  RELEASE=8、TRANSFER=16、QUERY=32。Resource Window `0x09000000..0x09010000`，4096 字节页对齐。
- INFO、LIMITS、Note 和知道一个 PFN 都不是授权。内核不接收用户指针；wrapper 的 extra 指针只在用户态解引用。

冻结常量/布局测试是宿主验证，不证明真实 trap；CPL3 反例另实测 KBD_RELEASE EAX/EDX 和未知调用 ENOSYS。
历史 ABI 1.1、原 1.2、两类 profile、可选设备与不同 LibOS 的真实矩阵见 [兼容性](phase9-libos-compatibility.md)。
