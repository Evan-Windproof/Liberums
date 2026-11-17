# 从零开发自己的 LibOS

不需要 Aelthuvor。你需要 C 指针/结构体基础，以及 GNU Make、GCC 的 freestanding i686 支持、Binutils、Python3。
运行实验另需 QEMU 和带 Python 的 GDB。Kernel ABI 与你自己的应用 API 是两层不同接口。

1. [Hello LibOS](01-hello.md)
2. [自己的 Runtime](02-runtime.md)
3. [物理资源与映射](03-resources.md)
4. [用户态内存策略](04-policy.md)
5. [抢占与 YIELD](05-preemption.md)
6. [主动 ACK 与安全失败](06-revocation.md)
7. [可选设备与交互](07-devices.md)
8. [多个独立 LibOS](08-multiple.md)
9. [QEMU/GDB 调试](09-debugging.md)
10. [空目录到自己的完整项目](10-own-libos.md)

所有代码都可在 [examples/libos](../../examples/libos/README.md) 找到。
`make test-libos-examples` 会从仓库外空目录生成/修改/重建程序，再检查真实 CPL3、CR3、资源和回收，
而不是只验证编译。ABI 的 26 个调用契约以 [public-abi](../public-abi.md) 为入口。
