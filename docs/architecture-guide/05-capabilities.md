# 5. Capability 与 Secure Binding

句柄包含 index 与完整 32 位 Generation；它不是裸物理指针，也不依靠索引保密。
内核另检查可信调用域的 ID/Epoch、资源状态和权限。

```text
handle(index,generation) + current domain(id,epoch) + required rights
    -> verify once -> install PTE -> MMU enforces ordinary accesses
```

这就是本项目内存 Secure Binding 的核心。代码见
[capability.c](../../kernel/capability.c)、[resource.c](../../kernel/resource.c)。

```c
struct resource_handle h = {index, generation};
/* 同一索引的新授权必须使用新代数，旧 h 不能复活。 */
```

实验：`make test` 的 Phase 4/8/9 检查伪造、旧代数、权限子集、别名、转移和 inbox；
在 GDB 查看 `resources[index-1].cap` 与实际 PTE。真实用户访问反例仍必须通过 syscall/#PF 验证，
GDB 的特权内存读取本身不证明用户能访问。

转移先撤旧 alias、清零、增加代数，再绑定目标 ID/Epoch 并放入单项 inbox。
常见错误：以为 transfer 保留数据，或缓存 ID 后误送给复用的新域。
局限：旧调用 22 只按此刻活跃 ID 路由，没有期望目标 Epoch 参数；MAX Generation 安全退休，不回绕。
