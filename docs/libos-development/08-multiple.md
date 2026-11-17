# Lesson 8 — 多个独立 LibOS

一次 QEMU `-initrd a.elf,b.elf` 创建真正的多个 Multiboot 模块；内核逐个校验 ELF，
创建私有 CR3、映像/BSS/用户栈。最多八个模块，最多四个并存域。

```sh
make run-multi MULTI_ELFS="build/examples/minimal/libos.elf,build/examples/memory-policy/libos.elf"
```

相同虚拟地址并不意味着共享页。测试检查两个池的 owner/epoch、PFN、CR3、PTE 与实际数据；
一个 #UD 后另一个仍验证数据并正常释放。外部 Aelthuvor Phase 4 与 bitmap 例子的同机实验另用：

```sh
make test-libos-compat PHASE10_AEL_BUILD=/path/to/Aelthuvor/build
```

这只是可选兼容实验；完成本教程、普通 make/test 和生成自己的 LibOS 都不需要 Aelthuvor。
不同 LibOS 不必有同一 Application API、allocator 或运行时，必须遵守同一 Kernel ABI/profile。
旧 TRANSFER 的 target ID 是调用时的活跃对象，不能当作缓存的稳定身份；转移前清零，不保留应用消息。
没有用户 Spawn、一般 IPC、父子进程、文件系统或跨域共享服务。
