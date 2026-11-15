# 外部 LibOS 加载入口

完整的格式边界、溢出检查、BSS、页权限和回滚实现见
[ELF32 加载器](phase5-elf-loader.md)。该实现已在先前 Phase 5 基础设施提交中存在，本次复用，
没有为 Aelthuvor 增加特权路径或改变原内置单页断言。

双仓库路径：

```text
Aelthuvor: C11 + startup.S -> 静态库 + 示例 -> 静态 ELF32
                                      |
QEMU -kernel liberums.elf -initrd <独立 ELF> -append external
                                      |
Liberums: Multiboot 保留模块 -> ELF/ABI 校验 -> 独立页表/段/BSS/栈
                                      |
                            iret -> CPL3 _start
                                      |
                LibOS query -> claim/map -> 用户块分配 -> exit
```

只有完整 `external` token 才走此路径，默认 `make run` 和原 Phase 2 initrd 保留测试不变。
Aelthuvor 的 ELF note 要求 ABI 1.1 的 PAGES/ELF32/COOPERATIVE/LIMITS，运行时再次查询。
缺失映像、损坏映像、版本不兼容或分配失败都不能激活不完整的用户域。

外部输入的虚拟地址不是物理分配权；内核分配独占物理页，只按经过校验的 LOAD 写用户 PTE。
内核完全不解析 Aelthuvor 的空闲表或策略枚举。普通 i686 无 NX，PF_X 只用于软件格式/入口校验，
不得把当前 RW/U/S 保护称为完整 W^X。

复现：`make test-libos AELTHUVOR=../Aelthuvor`。
`tests/phase5.py` 的原损坏 ELF corpus 和 LibOS 扩展模式分别验证格式边界与真实 LibOS 生命周期。
