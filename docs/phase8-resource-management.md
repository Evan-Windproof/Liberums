# Phase 8：多域资源与身份

| 身份 | 含义 | 失效边界 |
|---|---|---|
| Domain ID | 槽位编号 1～4 | 复用会重复该编号，不能单独表示永久身份 |
| Domain Epoch | 同一槽位的一次有效生命周期 | initialize 增加，MAX 后退休；Capability owner 同时绑定 ID/Epoch |
| Capability Generation | 某资源索引的一次授权代数 | claim/transfer 等更新；旧句柄必须拒绝，MAX 不回绕 |
| Revoke request ID | 撤销事件身份 | 全局单调，不复用；ACK 还需域身份与句柄匹配 |

公开资源常量 `LIBERUMS_RESOURCE_DOMAIN_LIMIT=4` 表示每域资源配额，
不是域数。公开 ABI 1.2、调用号、返回值、结构及 feature mask 均未改动。
全局资源表仍 16 项、每资源最多 4 个绑定；撤销表扩为四域各 2 项共 8 项。

用户只提供 handle 索引/Generation、受限虚拟地址、权限整数。
内核验证来源当前域的 ID/Epoch、资源状态和授权子集，再修改页表。
拥有者的物理页在别的 CR3 中不会产生用户映射；低地址物理 alias 均为 supervisor。
内核、页表、其他域内存与 VGA MMIO 都不能通过普通 RAM 分配或绑定取得。

## 转移和 Inbox

转移前检查资源是否可转移、是否处于撤销中、目标是否活跃、权限子集、目标配额、inbox 和 Generation。
任何拒绝都发生在更改所有权/内容前。成功时撤除原有者全部别名、清零页面，增加 Generation，
再绑定新目标 ID/Epoch、更新双方计数并发布 inbox。RECEIVE 消费 inbox，不额外创造资源。
未领取的 inbox 对应资源已经计入接收方配额，接收方退出时也必须回收。

旧 transfer ABI 只接收目标 Domain ID，表示**此刻该槽位的活跃域**；它不携带期望目标 Epoch。
这保持二进制兼容，却不是可长期保存的接收方身份令牌。
未来若需要防止用户策略将资源主动送给复用后的同一 ID，必须设计兼容扩展的目标 Epoch ABI；
当前没有声称提供这种路由保证。旧 A 的资源/设备句柄和授权仍绝不能作用于新 C。

## 撤销与设备

四域可同时拥有八个撤销事件。每域第 3 个请求返回 ENOSPC；挂起资源不能转移。
退出取消对应 ID/Epoch 的事件；重新创建后旧 request/handle 的 ACK 返回 ESTALE。
超时仍使用原强制回收路径，删除 PTE 后读访问产生用户 #PF，其他域继续执行。

VGA 与 Keyboard 是相互独立的独占授权；默认 multi 模块无两者权限。
显式模块序号授权通过原 `device_grants_apply` 执行事务。
若已有拥有者，新的失败创建只撤销自己刚成功取得的授权，不破坏旧拥有者。
退出/异常后清理队列、句柄 owner/Epoch、映射表和 IRQ1 使能，保留不可回绕的代数。
新域必须重新获得可信 grant 并 ACQUIRE；旧设备句柄被拒绝。
VGA PTE 保持固定 PA `0xb8000`、U/S+RW+PCD/PWT，未引入任意 MMIO/端口 I/O 接口。

真实 GDB 测试检查每轮所有 16 个资源记录、绑定 PTE、引用计数、owner ID/Epoch、
PFN 唯一性、物理页状态和最终空闲页数；不是只以串口没有 Panic 判定成功。
