# 3. 页、页表与地址

物理页是 4096 字节 RAM 单位，虚拟地址必须经过当前 CR3 所指的页目录/页表翻译。
32 位普通分页：VA 高 10 位选择 PDE，中 10 位选择 PTE，低 12 位是页内偏移。

```c
unsigned directory_index = va >> 22;
unsigned table_index = (va >> 12) & 1023;
unsigned offset = va & 4095;
```

[physical_pages.c](../../kernel/physical_pages.c) 区分 FREE/ALLOCATED/RESERVED/KERNEL；
[paging.c](../../arch/x86/paging.c) 操作可信页表，
[user_space.c](../../arch/x86/user_space.c) 为外部 ELF 建私有映像/BSS/四页用户栈。
内核 alias 为 supervisor，用户不能因知道物理地址就直接访问。

```text
CLAIM -> zero RAM -> handle -> MAP(valid Resource Window VA) -> PTE -> ordinary loads
RELEASE/revoke -> delete aliases -> flush TLB -> zero -> free
```

实验：`make test-libos-examples`；GDB 在用户 `example_observed` 查看 `$cr3`，
读取 `domains[0].space.pages[0]` 的目录与 VA `0x0900f000` 对应 PTE。
详细手工翻译见 [调试指南](../debugging-guide.md)。

常见错误：混淆 PFN、物理地址与用户 VA；删除 PTE 后忘记 TLB；把 VGA 当 RAM。
局限：Resource Window 只有 16 个 VA 页位、每域资源配额四项；i686 无 NX，本项目的 ELF RX/R/RW 检查不是硬件完整 W^X。
