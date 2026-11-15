# Visible Revocation：从通知到强制回收

## 状态和责任

```text
OWNED --可信内核 request--> REVOKE_PENDING
  |                            |-- 所有者 ACK --> 撤除绑定、清零、FREE
  |                            |-- 期限已到 --> 强制撤除、清零、FREE
  |                            |-- release / 域退出 --> 同一回收，取消事件
  +-- 自愿 transfer --> 换 owner/epoch，generation++，旧映射全撤除
```

请求不是改名后的 page_release：它先通知，保留一段响应时间，用户仍可读取旧绑定、
解除别名或整理数据，但新 map 和 transfer 返回 EBUSY。内核按期限保留最终回收权。
普通释放、转移、ACK 和强制超时有不同入口与统计，复用真正的安全回收步骤。
只有内核可信策略可以调用 `revoke_request(owner, handle, delay, reason)`；没有任意用户发起撤销的 syscall。
本阶段只提供机制与显式诊断压力源，自动选择驱逐对象属于后续策略工作。

事件保存完整 index/generation、owner ID/epoch、reason、deadline、request ID。
每域两个槽，共四个固定内核事件记录；一页资源可以有一个当前请求。
队满返回 ENOSPC，重复请求 EBUSY，时间/ID 溢出 EOVERFLOW，都不改变所有权或覆盖事件。
无等待用户指针、信号栈、回调或依赖被回收页的消息缓冲。

## ABI 1.2 的三个新调用

调用方式保持 int 0x80；EAX 为号/返回状态，EDX 为附加输出。常量在 `abi/event.h`。

| 号 | 输入 | 成功输出 |
| --- | --- | --- |
| 24 REVOKE_POLL | 无 | EAX=0，EDX=本域最早未完成 request ID |
| 25 REVOKE_INFO | EBX=request ID，ECX=field | EAX=0，EDX=完整 32 位字段 |
| 26 REVOKE_ACK | EBX=index，ECX=generation，EDX=request ID | EAX=0 |

INFO field 0=index、1=generation、2=reason、3=deadline；原因 1=PRESSURE、2=TEST。
POLL 不消费事件，无事件 ENOENT；INFO/ACK 查不到该域当前请求为 ESTALE，错误字段 EINVAL。
POLL/INFO 失败 EDX=0；ACK 不使用 EDX 作为结果。
所有者信息来自可信调用域，不接收 owner 参数，也不给用户读取别域事件的接口。
默认兼容模式三个调用均返回 EPROTONOSUPPORT，POLL/INFO EDX=0。

```c
uint32_t request;
if (liberums_call(SYS_REVOKE_POLL, 0, 0, 0, 0, &request) == 0) {
    /* 用 INFO 分别读取 index、generation、reason、deadline；自行整理数据。 */
    liberums_call(SYS_REVOKE_ACK, index, generation, request, 0, 0);
}
```

request ID 的最高位也可能为 1，因此把它放 EDX，EAX 单独报告状态，不把高位误判为负错误。
ACK 是“放弃继续使用”的确认，内核仍检查完整代数、请求身份和 owner epoch，并亲自撤掉全部别名。
到 deadline 当刻 ACK 已过期，先执行强制回收再返回 ESTALE。重复或旧 ACK 不能重复释放。
release 和域退出取消记录；pending 禁止转移，资源后来转移/复用时旧请求不再匹配。

## 为什么必须先撤映射，再交给新所有者

资源表记录最多四个别名，回收遍历全部 binding；PTE/PFN 不一致会 Panic，而非悄悄遗漏。
活动空间移除 PTE 时 INVLPG；最后一个绑定删除后清 PDE 并重载当前 CR3，再清零/释放动态表。
强制超时通常在内核 CR3 下处理，旧域已经切出，之后重新激活它会重载 CR3、刷新非 global TLB。
当前无 PGE、无 PCID、单 CPU，无另一 CPU 缓存翻译的情况。

只有所有旧映射消失后才清零物理页、标记 FREE、减少 owner 配额。下一次 claim 更新 generation、
再次清零并建立新绑定。用户保留旧 VA 只会触发 #PF，不能写新所有者的页。
B 的重新交付实验核对同一 PFN、更新的代数、1024 个零字、实际新写入，以及 A 的真实 #PF。
旧数据页可以回收，正在执行的内核栈、页表、映像、用户执行栈均不作为这类资源交付。

## 与 LibOS 的边界

内核保证可靠记录、身份检查、有限等待和最终保护。LibOS 决定缓存驱逐、搬迁数据、
提前释放还是 ACK，不把它的分配器算法迁进内核。
没有通用 DMA/设备撤销、异步回调、共享资源派生授权、信号或 SMP shootdown。
