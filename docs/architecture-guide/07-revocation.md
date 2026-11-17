# 7. 可见撤销与旧指针

内核可先通知资源拥有者，给它在 deadline 前处理的机会；不是先偷偷改用户对象。
[revoke.c](../../kernel/revoke.c) 提供 POLL/INFO/ACK 与到期完成；
[resource.c](../../kernel/resource.c) 移除所有 alias、清零和归还 RAM。

```text
trusted request -> POLL -> INFO(index/gen/reason/deadline) -> ACK
                -> timeout if no timely ACK
both completion paths -> PTE removal + zero + free
```

```c
/* 在已无活跃对象的页上，LibOS 才能安全放弃该页并 ACK。 */
int status = pool_ack_idle(&pool);
```

实验：`make test-libos-examples` 的 ACK、safe、fault 三场景；
`make test-phase9-e2e PHASE9_AEL_BUILD=...` 另验证未修改的外部 SDK。
GDB 应核对实际 request/owner/Generation/deadline、PTE=0、PAGE_FREE 和 1024 个零字。

常见错误：认为 QUERY 成功就把页 pin 住，或误把设备 revoke 当普通 RAM E2E。
局限：丢失的指针不会自动迁移/恢复；访问可能真实 #PF。请求仅由可信诊断触发，普通用户没有撤销别人的 syscall。精确计数边界的 native 测试与真实 IRQ 时间测试分开统计。
