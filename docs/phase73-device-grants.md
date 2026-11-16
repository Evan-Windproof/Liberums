# 可信外部设备授权

## 授权来源

QEMU 的 kernel command line 是本实验的可信启动配置输入，**不是 Secure Boot、签名校验或
抵抗恶意启动者的安全边界**。ELF bytes、文件名、entry、PT_NOTE 和程序自称角色都不构成授权。

| 启动 token | 作用 |
|---|---|
| `external` | 原双轮外部 ELF；默认无设备权限 |
| `external-console` | 选中外部路径、IRQ0 profile、VGA RW + keyboard grant，默认一轮 |
| `external-vga` | 在已选外部路径请求 VGA RW |
| `external-vga-readonly` | 请求 VGA R，若同时出现则缩减 WRITE |
| `external-keyboard` | 请求键盘；启用 IRQ0 profile 以便 IRQ1 可交付 |
| `external-once` / `external-repeat` | 显式一轮 / 双轮（repeat 优先） |

单独出现设备 token 而未选择外部路径不会授予权限。`preempt` 保留原语义。
不要求用户程序是 Console；换成任何经过 ELF/ABI 校验的程序，权限仍只由可信配置决定。

## 顺序与事务

1. 读取有界启动配置；若请求 VGA，在分页前探测硬件，记录是否可用。
2. 按原方式验证 ELF 与 ABI，创建独立空间和 domain epoch。
3. `device_grants_apply()` 验证有限权限 mask，先授予 VGA，再初始化/授予键盘。
4. 失败只回滚本事务已经发出的 grant；不能收回另一域原有授权。
5. 外部启动器销毁未执行的空间、保持 program UNUSED，验证 free count 恢复。
6. 成功后用户通过旧公开 ACQUIRE/MAP/POLL 接口取得自己的 handles。

`keyboard_abort_start()` 只允许撤销无 grant 的临时初始化，不影响已有 owner。
VGA 不可用返回 ENOENT；PS/2 初始化失败返回 EIO；busy/grant 失败返回错误，不能带着半个 bundle 继续。
旧 ELF 拒绝/部分页分配失败仍使用原地址空间回滚，未重写 loader。

PIT/IRQ0 的准备沿用原启动编排，在 ELF 校验前确定运行 profile；CPU 此时 IF=0，
不会运行未校验程序，也不因此授予设备。VGA 需在分页去掉 BDA 所在低页前探测；
这些早期硬件准备与域授权分开。PS/2 初始化、设备 grant 均在 ELF 校验和空间创建之后，
失败时回滚新域，不要求重写原 timer/ABI profile 模型。

## 保护不变量

VGA 和 keyboard 保持各自的 singleton grant、domain ID/epoch、完整 generation、独占状态。
RAM handle 不能进入设备 namespace；只读授权不能映射 WRITE；普通 ELF 默认没有任何设备 grant。
相关机制沿用 Phase 7.1/7.2，未增加公开授权 syscall。

`device_grants_revoke(owner, mask)` 支持分别收回 VGA 或键盘：
撤键盘只清事件/失效其 capability/屏蔽 IRQ1，VGA 仍可用；撤 VGA 只删除 binding/TLB/页表 RAM，
键盘 capability 仍可 POLL/STATS。旧映射再访问 #PF，旧 handle 再使用失败。
EXIT、FAULT、计数耗尽终止仍由调度器的既有资源清理路径处理。
VGA 页不进入普通 RAM 清零/free/transfer；显示内容的保留语义不变。

## 验收与测试控制边界

真实 QEMU 测试覆盖正常 grant、只读写拒绝、两轮 epoch/generation 复用、独立强制撤销、
PS/2 缺失导致的 VGA grant 回滚、普通 ELF/同名文件/相同 ABI note 无权限，以及退出/故障清理。
busy owner、非法 mask、grant 失败后保留其他 owner 等边界还用实际保护模块的 native suite 检查。

强制撤销实验使用显式可信 `selftest=device-revoke` 和 GDB 请求，仅调用已有内部 revoke，
不发 grant。入口位于 `tests/runtime_checks.c`，普通外部程序不能写这个内核状态，也没有对应 syscall。
GDB 只向测试程序的私有控制变量写请求；键盘数据必须经真实 QMP → PS/2 → IRQ1 到达，
没有用内存写队列或串口字符串伪造输入。

未来用户态启动器需要独立、可检查的权限委托协议；本阶段没有实现无限制委托。
