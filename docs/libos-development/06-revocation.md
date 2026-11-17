# Lesson 6 — 主动 ACK 与强制失去资源

普通用户没有 REVOKE_REQUEST。教学实验由显式 `selftest=phase9` 的可信诊断提出压力请求，
真实用户库只通过 POLL、INFO、ACK 响应。
[pool_ack_idle](../../examples/libos/memory-policy/pool.c) 读四个字段、匹配 handle/Generation，再 ACK 空闲页。

```c
uint32_t request;
int error = liberums_call(SYS_REVOKE_POLL,0,0,0,0,&request);
/* 检查 error，INFO 取得 index/gen/reason/deadline，再验证是不是自己的空闲页。 */
```

正常例子先归还所有对象而保留页，随后 ACK；不能把仍使用的对象随意 ACK。
超时例子保留活跃对象但不 ACK，真实 PIT 到期后 PTE 删除、页清零；
模式 2 检测 ESTALE 并安全退出 96，模式 3 的旧指针读会真实 #PF。

```sh
make test-libos-examples  # 三场景自动执行，并验证另一个池域继续运行
```

手工深入：用 `make debug QEMU_APPEND='multi-external selftest=phase9'`
并设置 `QEMU_INITRD=build/examples/memory-policy/libos.elf`。
在用户 `example_observed` stage 1 设置 `lesson_command=1`；stage 2 时从
`pool.handle` 与 `current_domain->epoch` 填写 `phase9_control` 的 ID/Epoch/index/generation/delay/submit。
停在 `phase9_request_done` 检查真实 request/deadline，然后在用户 `libos_yield` 停点设置 `lesson_gate=1`。
不要在内核 CR3 下直接写用户虚拟地址；先回到该域的用户停点或明确翻译它的私有物理页。
可对照 `tests/phase10.py` 的完整可运行 GDB 驱动。

不要手工改 PTE 或伪造事件。成功 QUERY、一次 heap check 都不能关闭后续撤销竞态；
本机制不提供透明恢复、指针迁移、pin 或可恢复用户缺页处理。
