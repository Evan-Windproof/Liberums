# 内核架构教程

面向会写 C、刚开始学习 x86 保护机制的读者。顺序是概念、最小例子、真实源码、实验。
不要先通读全部历史日志；先运行 [Hello LibOS](../libos-development/01-hello.md)。

1. [Exokernel 与 LibOS](01-exokernel.md)
2. [从 Multiboot 到 C](02-boot.md)
3. [物理页、分页与映射](03-memory.md)
4. [用户域与身份](04-domains.md)
5. [Capability 与安全绑定](05-capabilities.md)
6. [CPU、中断与恢复](06-interrupts.md)
7. [可见撤销与失效指针](07-revocation.md)
8. [VGA 与键盘保护](08-devices.md)

本指南描述教学发布基线。旧阶段记录见 [历史索引](../history.md)，其当时结论不自动代表今天。
