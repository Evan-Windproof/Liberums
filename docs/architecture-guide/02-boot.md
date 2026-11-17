# 2. 从 Multiboot 到 C

QEMU `-kernel build/liberums.elf` 装入带 Multiboot v1 头的内核，EAX/EBX 传递 magic/info。
它与 `-initrd user.elf` 的外部用户模块是两个不同输入。

```text
Multiboot -> _start -> flat GDT/segments -> zero BSS -> boot stack
          -> kernel_main -> COM1 -> IDT -> boot map/reservations -> paging
```

[boot.S](../../arch/x86/boot.S) 保存加载器参数，建立自己的栈再按 cdecl 调用 C；
[main.c](../../kernel/main.c) 编排初始化，
[boot_info.c](../../kernel/boot_info.c) 检查长度/模块边界，
[serial.c](../../kernel/serial.c) 提供 COM1 诊断。

```asm
movl $kernel_stack_top, %esp
pushl %ebx
pushl %esi
call kernel_main
```

实验：终端一 `make debug`；终端二 `gdb build/liberums.elf`，执行
`target remote :1234`、`hbreak kernel_main`、`continue`、`info registers eax ebx esp`。
GDB 中调用约定和优化可能影响参数显示，原始汇编入口证据在 `tests/entry.gdb`。

常见错误：将用户 ELF 当成内核、忘记 BSS 清零、在有效栈建立前调用 C。
局限：这里是传统 i686 PC/Multiboot，未提供 UEFI、任意板卡或通用引导器支持。
