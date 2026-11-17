# Lesson 1 — Hello LibOS

先在 Liberums 根目录构建 `make -j2 sdk libos-examples`，再 `make run-libos`。
COM1 中会出现 `Hello from my independent LibOS`；程序 EXIT 后 QEMU 仍停留，内核不自动关机。

一个最小项目需要自己的 `start.S`、C runtime/main、linker script，以及公共 ABI 头。
[完整入口](../../examples/libos/common/start.S) 只整理用户栈并进入 C，不执行 CLI、端口 I/O 或改页表：

```asm
.code32
.section .text.entry,"ax"
.global _start
_start:
    xorl %ebp,%ebp
    andl $-16,%esp
    call libos_start
    ud2
```

C main 使用这个 LibOS 自己的应用 API：

```c
#include "runtime.h"
int libos_main(void) {
    return libos_log("Hello from my independent LibOS\n") ? 92 : 0;
}
```

runtime 使用 SDK 的 `liberums_call`，其内联 `int 0x80` 只传整数寄存器。
链接器生成 ET_EXEC/ELF32/i386，入口位于 `0x08000000`，text/ro/data 页不重叠，
包含原始 36 字节 ABI Note。不要用宿主 libc/startup 或 Linux syscall。

检查：`readelf -h -l build/examples/minimal/libos.elf`、`nm -u ...`（应无未定义符号）。
真实运行：`make test-libos-examples` 的 minimal 场景在用户观察点核对版本与实际 CS=1b/CR3。
练习：只改消息再运行；受限 LOG 一生最多 128 字符，长消息可能返回 EAGAIN。
