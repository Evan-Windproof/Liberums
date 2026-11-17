# Lesson 9 — 用机器状态判断

[调试指南](../debugging-guide.md) 给出完整命令、PDE/PTE 公式及常见故障。
基本流程是先装内核符号，再 `add-symbol-file` 用户 ELF；同一 VA 在不同 CR3 可以属于不同代码，
断点必须核对 `current_domain->id`，不能按 PC 一样就当成同一程序。

```gdb
target remote :1234
hbreak kernel_main
continue
add-symbol-file build/examples/memory-policy/libos.elf
hbreak *example_observed
continue
info registers cs ss eip esp eflags cr3
p current_domain->id
p current_domain->epoch
p/x tss.esp0
```

在对应域的用户停点才可按其 VA 查看 `pool` 和 payload。
GDB 对物理 alias 的读取是特权调试，不证明 Ring 3 能访问；实际保护要看用户指令产生的 #PF。
#PF 的 CR2 是出错地址，error 位区分 present/write/user；EIP 是真实出错指令，不能跳过来假装恢复。

使用硬件断点避免写只读代码，至多四个同时活动。中断到达没有固定顺序，
用真实阶段/序号判断，不按输出行顺序猜生命周期。COM1 是诊断，不能替代 CR3/PTE/ownership 和清理检查。
