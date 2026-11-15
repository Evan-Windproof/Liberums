# PIT、PIC 与中断现场

## 两块硬件分别做什么

PIT channel 0 用 1,193,182 Hz 输入时钟除以 11,932，模式 2 周期产生 IRQ0，
约 99.9985 Hz，即每 tick 约 10.00015 ms。8259 PIC 接收 IRQ 并提交中断向量。
`pic.c` 用 ICW 初始化主/从片，重映射为 32..39、40..47；异常为 0..31，系统调用为 128，
因此不会混用。主 IMR=0xfe、从 IMR=0xff，仅开放 IRQ0，连从片级联 IRQ2 也保持屏蔽。

IDT[32] 是 DPL0 的 32 位 interrupt gate。Ring 3 的 `int $32` 不获授权。
CPU 异常是指令或硬件错误产生的同步事件，部分带错误码；IRQ0 不带错误码。
IRQ 汇编显式补 `error=0, vector=32`，但不复用错误码异常桩。

## IF 与栈

EFLAGS.IF 决定是否接受可屏蔽外部中断。中断门在进入时自动清 IF，IRET 恢复保存的标志。
默认兼容模式始终保持用户 IF=0。显式抢占模式只在 IRET 到用户时设置 IF=1。
所有内核 C、系统调用、异常和调度者都以 IF=0 运行，不会在修改资源表中途切域。
诊断启动时唯一的 `sti; hlt; cli` 位于尚未创建域、没有资源临界区的位置，用来验证内核来源 IRQ。

从用户进入 IRQ 时，CPU 使用 TSS.SS0=0x10、ESP0=当前域可信栈顶，保存外层 SS/ESP、
EFLAGS、CS、EIP。从内核进入 IRQ 时 CPU 只保存后三项；代码只有确认 CS.RPL=3 后才读取外层栈。
不依赖用户 ESP 指向可读内存，因此坏用户栈也不能控制 Ring 0 的现场布局。

```text
低地址 IRQ frame
  GS FS ES DS                    16 bytes
  EDI ESI EBP saved_ESP EBX EDX ECX EAX
  vector error
  EIP CS EFLAGS
  user_ESP user_SS                仅从 Ring 3 进入时存在
高地址（TSS 栈顶）
```

`irq.S` 先 PUSHAD，再借用 AX 读取段选择子，防止覆盖尚未保存的 EAX。
保存选择子后装内核段、CLD、按 i386 ABI 对齐栈再调用 C。用户的 DF 不可泄漏到 C 字符串操作。
段状态独立存放，不改变原先 60 字节 `user_context`，原异常/syscall 测试仍检查原布局。

## EOI 与返回

`timer_dispatch()` 每次恰好向主 PIC 发一个 EOI，不向从片发送；EOI 在离开中断栈之前完成。
用户来源保存上下文后经 `arch_user_leave()` 回到可信调度栈，不从 IRQ 原栈直接调度下一域。
内核来源不切换 CR3、不调度、不释放资源，汇编恢复段、所有 GP、EFLAGS 和原栈，IRET 返回。
IF=0 避免可屏蔽中断嵌套；NMI、机器检查仍沿用致命异常处理，未实现紧急故障栈。

## 参考与范围

实现依据 Intel SDM Volume 3 的 interrupt/task stack/IRET/分页章节，以及 Intel 8259A、8254 数据手册。
参考入口：[Intel SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)、
[QEMU PC 系统](https://www.qemu.org/docs/master/system/target-i386.html)。
本次只验证 QEMU pc/TCG/qemu32/单 CPU，未宣称覆盖真实 PC、APIC、HPET、SMP 或 DMA。
