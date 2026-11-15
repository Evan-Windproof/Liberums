# 理解 Phase 2 的内存保护

## 物理地址与虚拟地址

物理地址描述机器的 RAM 或设备地址。C 程序里的指针在分页开启后使用线性/虚拟地址。
本项目的 GDT 使用基址为零的平坦段，因此分段得到的线性地址与指针数值相同。

分页负责把虚拟页翻译到物理页，还要检查权限。本阶段使用 identity mapping：

```text
虚拟 0x00100000 --> 页目录 --> 页表 --> 物理 0x00100000（只读、supervisor）
虚拟 0x00000000 --> not present      --> #PF
虚拟 0x04000000 --> 无 present PDE  --> #PF
```

两边数值相同，不代表分页没有作用：CPU 仍然检查 present、读写和特权位。
当前映射最多低 64 MiB 中的 RAM 与必要启动区，不代表机器只能有 64 MiB RAM；
高于这个范围的地图记录仍保留 64 位信息，但不会分配或映射。

## 页与两级页表

一个普通 i686 页为 4096 字节。虚拟地址分为 10 位目录索引、10 位表索引和 12 位页内偏移：

```text
31          22 21          12 11           0
+-------------+--------------+--------------+
| 目录索引 10 | 页表索引 10  | 页内偏移 12 |
+-------------+--------------+--------------+
```

一张目录有 1024 个 PDE，每个 PDE 指向一张包含 1024 个 PTE 的表。
一个 PTE 指向一页，低 12 位可存属性，因为物理页地址本来就是 4 KiB 对齐。

```c
entry = physical_address | 1u | 2u;
/* bit 0: present; bit 1: writable; bit 2 留零: supervisor */
```

目录层和页表层都会限制权限，因此不能只检查 PTE。
本阶段目录前 16 项指向 16 张页表，覆盖最多 64 MiB；其余 PDE 保持零。
页表、栈、IDT、状态表与内核代码都在加载器装入的内核范围中，切换时不会“丢失”。

`__kernel_readonly_end` 标记代码和常量占据的页的末尾，后续可写数据从新页开始。
仅改变 ELF 的 R/W/X 标志不会改写 CR0 或 PTE；这里显式构造了真实硬件页表。

## CR0、CR2、CR3

| 寄存器 | 本阶段用途 |
| --- | --- |
| CR0.PE | bit 0，保护模式；Phase 1 已开启 |
| CR0.PG | bit 31，开启页表翻译 |
| CR0.WP | bit 16，让 supervisor 写操作也遵守只读页保护 |
| CR2 | 最近一次 Page Fault 的线性地址，在处理入口尽早读取 |
| CR3 | 页目录的物理地址，必须按 4 KiB 对齐 |

先构造页表、装入 CR3，再打开 PG/WP。我们不搬动内核，也不改变当前指针数值。
若当前 EIP、栈或异常入口没有正确映射，CPU 可能连错误报告都执行不了。
当前实际观察到 CR0=`0x80010011`，包含 PG、WP、PE；其他已有位保持原值。

## Page Fault 不是单纯“内存不够”

#PF 是向量 14。它可能表示地址没有映射，也可能表示写入只读页。
CPU 压入的错误码说明访问原因：

- bit 0：0 表示不存在；1 表示权限保护故障。
- bit 1：0 表示读；1 表示写。
- bit 2：0 表示 supervisor；1 表示 user。
- bit 3：页表保留位违规。

例如测试写入只读内核页得到 error=`3`：present 保护故障 + 写 + supervisor。
这证明即使内核位于 ring 0，CR0.WP 也能阻止该写操作。
处理器报告的 EIP 指向故障指令，CR2 指向被访问的地址，二者用途不同。
本阶段报告后停止，不实现自动调页或恢复策略。

## TLB 为什么需要同步

CPU 会缓存地址翻译。把内存中的 PTE 清零，不会自动清除已缓存的翻译。
`paging_unmap_allocated()` 写 PTE 后执行 `invlpg`，让下一次访问重新查表。
测试先读写该页以使用映射，然后清 PTE，再访问并观察 #PF。
当前只有单 CPU，没有 SMP TLB shootdown，也没有 global page。

## 状态追踪不是访问授权

物理页表中的 FREE/RESERVED 是内核的软件记录；PTE 是 CPU 的访问规则。
修改一个软件状态字节不会自动撤销映射。
当前所有代码都可信且位于 ring 0，FREE RAM 仍有 supervisor identity mapping。
因此我们不能声称“已分配页只能由持有者访问”，更没有 secure binding。

未来把资源交给用户域时，内核必须检查所有者和权限、建立受控映射、协调归还/撤销，
转交前清零数据。LibOS 再决定如何用得到的页实现堆、缓存或栈。
这就是保护与管理分离：内核保证边界，LibOS 决定用途。

## 当前保护的边界

- 没有 PAE/NX，普通 32 位页表不能给出完整的“不可执行数据页”保证；不能宣称完整 W^X。
- 内核数据、栈和页表仍可由可信 ring 0 写入；这里只保护代码和只读数据免于普通写操作。
- 需要未来的 ring 3 和独立地址空间，才能测试真正的跨用户域隔离。
- 不映射页零，也不等于已经给栈配置了 guard page 或安全处理栈溢出。

## 采用的规范

实现采用公开的 x86/Multiboot 格式，没有复制其他 OS 的内存管理实现：

- [Intel 64 and IA-32 SDM](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html)：
  Volume 3A 的 Paging、Interrupt and Exception Handling、Protection 各章；
  依据其 i686 两级分页、WP、#GP/#PF 错误码与同级中断栈约定设计。
- [GNU Multiboot v1](https://www.gnu.org/software/grub/manual/multiboot/multiboot.html)：
  Boot information 与 BIOS memory map；每条记录 size 不含 size 自身，模块结束地址不含最后一个字节。
- 开发时核对了 [GRUB 的 Multiboot 格式定义](https://github.com/rhboot/grub2/blob/master/include/multiboot.h)
  中的字段顺序、模块 16 字节布局和可变记录头，代码自行实现有界解析。
