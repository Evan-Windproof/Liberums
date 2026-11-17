# Phase 9：四种身份与目标路由

| 标识 | 所表示的对象 | 防复用规则 |
|---|---|---|
| Domain ID | 有界槽位 1～4 | 编号会重复；本身不是稳定身份凭证 |
| Domain Epoch | 同一槽位的一次生命周期 | initialize 增加，MAX 后退休；资源和设备 owner 同时绑定 ID/Epoch |
| Handle Generation | 某资源索引的一次授权 | claim/transfer/acquire 推进，完整 32 位比较，耗尽不回绕 |
| Revoke request ID | 一次有时限的撤销 | 不重用；POLL/INFO/ACK 还检查来源 Epoch 与句柄 |

身份由 trap 所属的 kernel domain 得出，用户参数不能冒充它。
已释放句柄 ESTALE；他域的有效句柄 EPERM；旧事件 ACK ESTALE。
PREPARING/RECLAIMING/终止槽位不在 domain_lookup 中，不能作为 transfer 接收方。
退出时 inbox 对应资源也必须回收，不能交给下一 Epoch。

## 保留 SYS_PAGE_TRANSFER=22 的理由

旧语义是“送给调用时此 ID 的活跃域”，不是“送给调用者曾经见过的那次 Epoch”。
新对抗实验将 ID 3 从 Epoch 1 终止并复用至 Epoch 2，再用缓存 ID 3 转移：
内核将清零后的页交给 `(3,2)`、提升 Generation、撤除原映射；原域旧句柄仍失效。
这证明现有路由语义，不能把调用者选错接收方等同于新域取得旧 Capability。

设计审查后选择不增加 Epoch-aware Transfer：本阶段无需新增目标稳定性保证，
且没有公开目标 Epoch 发现/授权协议，不能仅增加一个参数就声称解决完整的对端身份问题。
未来若需要该保证，应另行设计可发现的兼容扩展，明确目标 ID/Epoch、拒绝时不变性及迁移方式。
不改写旧编号、旧 ELF Note 或旧语义，也不借此增加 Spawn/IPC 服务。

实际 GDB 检查包括 owner ID/Epoch、代数、两个 CR3 的 PTE、物理内容、inbox、
专属内核栈清零及最终资源回收。128 轮复用中槽 1 Epoch 从 1 增至 129，
旧句柄每轮都从新用户程序发出 QUERY/MAP 等真实反例；其他三域保持 Epoch 1 和各四项资源。
