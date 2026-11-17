# 4. 用户域与身份

域是这套内核保护一个不可信执行环境的单位，不是 POSIX 进程。
外部用户从 `iret` 进入 Ring 3；发生 syscall/exception/IRQ 时，CPU 使用 TSS ESP0 进入该域专属内核栈。

```text
UNUSED -> PREPARING -> READY -> RUNNING -> READY (IRQ0/YIELD)
                                     -> EXITED/FAULTED -> RECLAIMING -> UNUSED
```

[domain.c](../../kernel/domain.c) 验证 trap 的 CS、CR3 和内核栈来源；
[domain_pool.c](../../kernel/domain_pool.c) 负责安全复用，
[gdt.c](../../arch/x86/gdt.c) 配置 TSS 与禁止用户端口 I/O。
每域内核栈 8192 字节，外部用户栈 4 页，二者不能互换。

```c
/* ID 是槽位，Epoch 是这个槽位的某一次生命。 */
/* 同一个 ID=1 可以先有 Epoch=1，再有 Epoch=2。 */
```

实验：`make test-phase9` 包含 128 次 EXIT/#UD 交替复用；GDB 的 `domain_pool_reaped`
应看到资源/事件/设备清理、用户空间销毁、上下文和内核栈清零之后才出现 UNUSED。
常见错误：把 ID 当永久身份，或在仍使用该 CR3/内核栈时释放它。
局限：最多四域，只由可信启动配置/诊断建立，没有用户 Spawn、fork/exec 或父子进程语义。
