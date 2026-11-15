# 外部 ELF32 的校验和装载

## Program Header 与 Section Header

编译器/链接器使用 section 组织 .text、.rodata、.data、.bss、符号和调试信息。
运行时装载主要依据 program headers，尤其 PT_LOAD。一个 LOAD 可以包含多个 section。
本加载器不依赖 section 表，测试中移除 section 表索引后仍可运行；调试符号不是运行要求。

PT_LOAD 的 offset 指向文件，vaddr 指向用户虚拟地址，filesz 是要复制的字节数，
memsz 是整个内存区域。`memsz-filesz` 的尾部必须为零，常见用途是 BSS。
不能把 memsz 当成文件大小，否则会越界读取未提供的文件字节。

## 明确的受限 profile

支持 ELF32、小端、SYSV OSABI 0、i386、ET_EXEC、版本 1、e_flags=0、52 字节 ELF 头、32 字节程序头。
文件最多 256 KiB，最多 16 个 program headers、8 个非空 LOAD、64 个映像页。
仅接受 PT_NULL、PT_LOAD 和一个规定格式的 PT_NOTE；DYNAMIC/INTERP/TLS/GNU_STACK 等未支持类型拒绝。
外部项目应使用本仓库示例链接脚本的显式 PHDRS 和 `--build-id=none`，不能直接提交 Linux 可执行文件。

LOAD 必须：

- filesz <= memsz，文件区间和内存区间都在各自界限内，无加法溢出。
- 完全位于 `[0x08000000,0x08040000)`；不允许内核、资源窗口或栈地址。
- 可读，权限仅含 R/W/X；拒绝显式 W+X。
- align 为 0、1 或不超过 4096 的二次幂，满足对齐同余；offset/vaddr 页内偏移也必须一致。
- 页向外取整后与其他 LOAD 不重叠，避免一个硬件页合并出意外写权限。

入口必须指向某个可执行 LOAD 的实际文件字节，不能是 BSS、数据段或未映射区域。
p_paddr 不提供分配权，也不决定实际 PFN；内核总是取得自己的物理页。
Section Header 的内容从不作为内核指针或权限依据。

## 必需的公开 ABI note

一个 PT_NOTE 指向恰好 36 字节的 `struct liberums_abi_note`：

```c
const struct liberums_abi_note requirement
    __attribute__((section(".note.liberums"), aligned(4), used)) = {
        9, 12, LIBERUMS_NOTE_TYPE, "LIBERUMS", 1, 0,
        LIBERUMS_FEATURE_PAGES | LIBERUMS_FEATURE_ELF32
    };
```

字段是 namesz=9（含 NUL）、descsz=12、type=0x4c414249，名字填充到 12 字节，
再跟三个 uint32：major/minor/required_features。检查名字、长度、填充和描述符兼容性。
不兼容要求返回 -93；其余损坏格式返回 -8。note 可以同时属于只读 LOAD 和 NOTE。
自报兼容性并不能获取权限；所有地址、资源和陷入检查依然执行。

## 先解析，再分配

`elf_validate()` 按字节读 little-endian 字段，不把任意文件位置直接强转为未对齐结构指针。
先检查完整程序头表，再生成有界的可信 `elf_plan`。失败时清除 entry/计数，完全不分配物理页。
模块源来自已校验并保留的 Multiboot 数据，用户不能申请这些页，所以运行期间不会被用户改写。
仍然信任加载器正确描述物理内存；不声称可防御恶意固件/加载器。

只有成功计划才能传给 `address_space_create_external()`：

1. 申请并清零目录、用户页表，复制 supervisor-only 内核 PDE。
2. 按 LOAD 的页范围申请独占物理页，设置用户 PTE 和 RW 位。
3. 通过 supervisor 物理别名复制 filesz，未被复制的字节保持零。
4. 另外申请四页零栈；栈下方和映像/栈之间的空洞不映射。
5. 完整成功才标记 live，然后初始化域并 iret。

最多 70 个基础页（目录+用户表+64 映像页+4 栈页）。Phase 4 的资源页表独立惰性分配。
任何一步 ENOMEM 都在该空间激活前清零并释放全部已申请页，返回 -12，绝不留下半个可运行域。

## 多页权限与回收

PF_W 决定 PTE.RW；所有用户 PDE/PTE 都显式设置 U/S。内核低端映射仍 supervisor-only。
PF_X 用于入口校验和拒绝显式 W+X，但普通非 PAE i686 无 NX，数据/栈仍可能执行，不能宣称完整 W^X。

退出或用户异常先回到可信内核栈、恢复内核 CR3，使用原 resource_cleanup 撤销动态资源，
再销毁映像/栈页。清零所有私有内容之后才将页归还分配器。不会释放共享的内核页表。
输入拒绝、加载失败不妨碍 B 域运行，重复加载必须重新初始化数据、BSS 和栈。

ELF 字段意义参考 [System V ABI ELF 规范](https://refspecs.linuxfoundation.org/elf/gabi4+/contents.html)，
实际支持范围以本 profile 为准；没有实现动态链接、重定位、共享库或文件系统。
