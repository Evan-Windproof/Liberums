# QEMU/GDB 调试指南

```sh
make -j2 libos-examples
make debug QEMU_APPEND=multi-external QEMU_INITRD=build/examples/memory-policy/libos.elf
# 另一终端：
gdb build/liberums.elf
```

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
p pool
```

内核本身的 `_start` 与用户 `_start` 不是同一个入口。同一 VA 的用户符号可能属于不同 CR3，
请对断点加 `condition $bpnum current_domain->id == 1` 并核对来源。
本例外部用户栈范围 `0x083fc000..0x08400000`，TSS ESP0 指向独立 8192 字节内核栈顶。

## 翻译 Resource Window PTE

以下在该域空间 live 时由特权调试器读取，不修改任何页表：

```gdb
set $pd = (unsigned int*)current_domain->space.pages[0]
set $va = 0x0900f000
set $pde = $pd[$va >> 22]
set $pt = (unsigned int*)($pde & 0xfffff000)
p/x $pt[($va >> 12) & 1023]
```

必须先确认 PDE present；PTE 的页地址加 `va & 4095` 是物理字节地址。
U/S、R/W、Present 是硬件访问条件，Accessed/Dirty 可由 CPU 改变。
VGA 还需正确 PCD/PWT 与固定设备 PA。GDB 能读 supervisor alias 不意味着用户可读它。

## 看故障，不跳过故障

停在 `domain_fault` 检查 vector/error、`$cr2`、来源 EIP。
#PF error 低三位为 present/write/user；例如 user 非 present 读通常为 4，user 读 supervisor 已存在页为 5。
读取 `domains[i].fault_*` 与最终 last_state；不要调整 EIP 绕过错误来宣称保护成功。

| 问题 | 首先检查 |
|---|---|
| ELF 被拒绝 | ET_EXEC/i386、PT_LOAD/PT_NOTE、Note/features、段边界/页重叠 |
| runtime 立即退出 90 | version/features 与 Note 是否一致 |
| MAP 失败 | 正句柄、完整 Generation、rights、对齐 Resource Window、配额 |
| GTK 启动失败 | 本机 QEMU 是否有 GTK、桌面/WSLg；云端改用自动 none 测试 |
| 无键盘输入 | 显式 grant、IRQ1、正确 Poll、窗口焦点；不能用串口冒充 PS/2 |
| 旧指针 #PF | 是否已经 RELEASE/transfer/revoke；不存在透明恢复保证 |
| 多域停点混淆 | 当前 ID/Epoch/CR3；至多四个硬件断点，勿依赖固定 IRQ 顺序 |

`build/*-gdb.log`、`*-serial.log`、生成的 `.gdb` 保存自动实验；
`make test-libos-examples` 还记录仓库外编译命令、依赖与 link map。
