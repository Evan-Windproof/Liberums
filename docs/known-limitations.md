# 已知限制与保证边界

发布稳定的是教学范围与验证基线，不是生产级安全证明。

- i686 PC / Multiboot v1 / QEMU TCG 是实测目标；单 CPU，默认 32 MiB。没有任意硬件/UEFI/裸机兼容承诺。
- 最多四个并存域、八个 boot 模块。全局 16 个 RAM 资源、每域四个、每资源四绑定；Resource Window 只有 16 个 VA 页位。
- 撤销表八项、每域二项；计数/身份耗尽采取安全失败或退休，不提供无限运行保证。
- 合作模式 IF=0，只能主动让出；不合作程序可能占住 CPU。抢占模式也不承诺实时墙钟延迟。
- 内核单 CPU、不可抢占；没有 SMP/TLB shootdown、线程、一般 Spawn、fork/exec、POSIX、通用 IPC、文件系统、网络或动态链接器。
- 普通 i686 分页无 NX，ELF RX/R/RW 软件检查不是完整硬件 W^X；FPU/MMX/SIMD 不虚拟化，使用会被拒绝。
- 强制 RAM 撤销后旧指针不能透明恢复。QUERY/一次 heap check 不等于 pin；可能安全退出或真实 #PF。
- 旧 PAGE_TRANSFER=22 按此刻活跃 ID 路由，不含期望目标 Epoch；转移页会清零，不是保留消息的共享内存。
- VGA/Keyboard 分别独占；INFO/Note 不授予权限。无设备共享、窗口管理、多 Console 同屏或 GUI。
- VGA 屏幕 4000 字节按一页 4096 字节映射，尾部 96 字节是设备 padding，不能误称精确字节隔离。
- PS/2 队列 64 字节、US translated set 1 教学解码；溢出丢弃批次并要求重置 decoder，无通用输入法/鼠标。
- 可信诊断代码显式由 boot selftest 启用，仍编入教学内核；不是普通用户特权接口或生产资源服务。
- 128 轮复用是有界压力测试；未进行数十亿次真实 Epoch 复用或长期硬件 soak。
- 云端 none 后端的显存/renderer/模拟 PS/2 证明与本地 GTK、实体键盘不同；本阶段未人工验收本地窗口。

保护层次：软件先验证 handle/owner/rights/边界，MMU 再限制普通访问；可信调试器可读物理 alias，
不能用 GDB 的特权读取冒充用户访问成功。原始测试与条件见 [发布验证](release-validation.md)。
