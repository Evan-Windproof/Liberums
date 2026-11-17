# Lesson 5 — 抢占下的普通 C 程序

默认示例 Note 要求 PREEMPTION，内核允许用户 IF=1并用真实 IRQ0 收回时间片。
应用不需要主动保存 CR3、TSS 或 IRQ 寄存器；这些是内核保护机制。
你也不能用 CLI 或改 PIC/PIT 来控制调度。

```c
for (volatile unsigned i=0; i<12000000; ++i) {
    __asm__ volatile("" : : : "memory");
}
```

memory-policy 用这种有限非合作工作，在任意指令被暂停后继续检查 payload。
等待教学实验门闩时会 YIELD；测试另观察真实 IRQ0，绝不把 YIELD 当作抢占证据。

```sh
make run-multi MULTI_ELFS="build/examples/memory-policy/libos.elf,build/pool128/libos.elf"
make test-libos-examples
```

Round-robin 只对 READY 域仲裁，EXIT/#PF/#UD 不再运行；内核仍单 CPU、内核代码不抢占。
volatile 仅用于可观察教学输入/进度，不是线程同步保证。将来自己在一个域内实现协程是用户策略，
本阶段没有线程、SMP、实时份额或 CPU capability API。
