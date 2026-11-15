# Phase 3 模块与接口

Phase 1 的 `boot.S`、端口 I/O 和入口 GDB 断言保持原样。Phase 2 的地图解析与页状态
算法继续使用；本阶段围绕它们增加有界的用户执行机制，没有重建操作系统。

## `arch/x86/gdt.c` / `include/arch/x86/gdt.h`

负责扩展 GDT、描述 104 字节的 32 位 TSS、执行 `ltr`。
`gdt_init()` 保留原内核选择子 0x08/0x10，新增用户 0x1b/0x23 和 TSS 0x28。
`task_state` 按硬件字段布局定义，编译期断言验证总长和 I/O bitmap 偏移。
`tss_set_kernel_stack(top)` 更新 ESP0；SS0 固定为 0x10，I/O bitmap 在描述符范围之外。
GDT/TSS 是 supervisor 内核数据，用户不可修改。

无需硬件任务切换。CR3 和执行现场由软件管理，TSS 只提供特权级入口栈及 I/O 限制。
原三项引导 GDT 仍在 `_start` 使用；扩展 GDT 在 Phase 2 诊断之后加载，确保其非法
0x18 段选择子测试依然在原环境下成立。见 [特权级说明](phase3-privilege.md)。

## `arch/x86/user_context.S` / `include/arch/x86/user_context.h`

`user_context` 是原 52 字节异常前缀，加上仅跨特权级时存在的 user_esp/user_ss，总计 60 字节。
`arch_user_enter(context)` 保存调度者的 callee-saved 寄存器及内核 ESP，随后使用 iret 进入用户。
`arch_user_leave()` 丢弃当前域的异常/系统调用 C 调用链，回到可信调度者调用点。
这两个路径不同于调用普通 C 用户函数：最终用户指令实际由 CPL 3 执行。

`user_iret` 为初次进入和系统调用返回共用。它恢复通用寄存器、去掉归一化的 vector/error，
由 CPU 恢复 EIP/CS/EFLAGS/ESP/SS。DS/ES/FS/GS 固定归一为用户数据选择子；当前没有 TLS。
C 层先清除 NT/VM/IOPL/TF/IF，避免把用户设置的危险标志带入内核 iret。
进入用户前设置 CR0.TS，不开放尚未保存/恢复的浮点寄存器状态，实际测试 x87 产生 #NM。

## 扩展的 `arch/x86/idt.c` / `isr.S`

原向量 0..31 和 CPU 错误码归一规则不变。新增 IDT[128] 为 DPL 3 interrupt gate，
其余异常 gate 保持 DPL 0。公共入口保存寄存器并切回内核 DS/ES/FS/GS，清 DF 后调用 C。
系统调用返回可走 `user_iret`；yield、exit 和用户故障则进入调度路径。

`exception_dispatch()` 根据保存的 CS 区分来源，只有已验证的用户执行现场才交给域管理。
NMI、double fault、machine check 始终作为严重系统故障处理；Ring 0 错误仍输出原诊断并 Panic。
不能只看当前 handler 的 CS，因为 handler 本身总在 Ring 0。

## 扩展的 `arch/x86/paging.c` / `include/arch/x86/paging.h`

`address_space` 记录固定六页：页目录、普通用户页表、私有探测别名页表、代码、数据、栈。
`address_space_create()` 只接受内核给定的受信映像和两个固定探测窗口之一，内部逐页申请、
清零、建表；不对用户暴露任何物理地址映射接口。中途失败会清零和释放已申请页。
`address_space_activate()` / `address_space_kernel()` 切换 CR3。
`address_space_destroy()` 拒绝销毁当前 CR3；先清除映射结构，再清零所有页并归还。

内核低 64 MiB 映射沿用 Phase 2，始终 supervisor。普通用户页仅出现在指定窗口。
共享内核页表不是共享用户地址空间：这些页表没有 U/S 权限，且用户不能修改它们。

## `kernel/domain.c` / `include/kernel/domain.h`

`domain` 包含 ID、状态、地址空间、60 字节保存现场、退出码/故障信息、诊断计数和 8 KiB 内核栈。
仅两个固定槽位，内核栈永久属于内核 ELF；动态申请的六页在每轮结束释放。

- `domains_run()` 编排固定顺序的合作式执行、两轮生命周期及资源计数检查。
- `domain_from_trap()` 验证当前 RUNNING 域、CR3、保存 CS/SS 和现场所在的可信内核栈范围。
- `domain_yield()` 保存完整用户现场并标为 READY，回到调度者。
- `domain_exit()` 标为 EXITED 并记录标量退出状态。
- `domain_fault()` 记录向量/错误码/EIP/CR2，标为 FAULTED，结束该域，绝不跳过故障指令。
- `user_context_sanitize()` 限定返回 EFLAGS。

`domains_prepared()` / `domains_reaped()` 是无操作的 GDB 检查点，不包含“通过”逻辑。
页回收在切回内核 CR3、回到调度者栈之后执行；当前异常栈不会被就地释放。

## `kernel/syscall.c` / `include/kernel/syscall.h`

`syscall_dispatch(frame)` 只处理 yield、exit、单字符日志，未知号返回 -38。
验证域身份后才处理参数，不接受用户指针，也不调用用户指定的内核函数。
日志参数必须在 0..255，且每个域最多接受 128 次日志请求，避免诊断输出无限膨胀。
它不能阻止无限循环，占用 CPU 的问题仍需将来的抢占机制。
ABI 见 [特权级文档](phase3-privilege.md)。

## `user/` 与映像构建

`test_a.c`、`test_b.c` 是两个独立链接的用户入口；共享 `program.c` 和 `api.h` 中的最小调用封装。
`entry_checks.S` 验证寄存器/DF/NT 及执行故意非法指令。`user/linker.ld` 将代码固定在 0x08000000，
禁止隐含的 .data/.bss，映像不得超过一页；用户数据使用明确的私有页。
Makefile 分别生成可供 GDB/nm 查阅的用户 ELF，再 objcopy 为二进制；`user_images.S` 将其作为
只读字节嵌入内核。用户 ELF 不是由运行时 ELF 加载器装载，也不会在 Ring 0 调用其入口。

## 既有模块的小幅集成

`kernel/main.c` 在原初始化与兼容性信息之后调用 `domains_run()`，保持流程编排职责。
`boot_has_option()` 从原 selftest 的 token 匹配函数提取到 boot_info，供两个阶段共用。
`selftest_address_space_failure()` 仅在显式诊断时制造页耗尽并验证创建回滚。
Phase 1/2 测试脚本和断言未修改；新增 `tests/phase3.py` 链接原有测试启动辅助函数。

每个模块对应的实际 GDB/QEMU 验收见 [Phase 3 验收记录](phase3-validation.md)。
