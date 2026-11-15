# Phase 1 实际验收记录

日期：2026-11-15（Asia/Taipei）。本记录来自当前云环境实际执行，
不代表在真实硬件或另一台机器上的验证。

## 环境

| 项目 | 实际版本/配置 |
| --- | --- |
| 宿主系统 | Debian GNU/Linux 13，x86_64，普通用户 |
| 编译器 | GCC 14.2.0，`-m32 -march=i686 -std=c11 -ffreestanding` |
| 链接器 | GNU Binutils 2.44，`elf_i386`，无宿主 libc |
| 构建 | GNU Make 4.4.1 |
| 模拟器 | QEMU 10.0.13，TCG，qemu32，32 MiB，单 CPU |
| 调试器 | GDB 16.3，QEMU 回环 GDB stub |

云镜像未提供 QEMU/GDB，也没有 sudo 或系统包安装 helper。
系统 APT 源的 snapshot.debian.org 请求被代理拒绝；因此在 `/workspace/.liberums-tools`
使用独立 APT 列表，从可访问的 Debian 官方镜像下载软件包并解包至用户目录。
保留 Debian archive keyring 签名及 APT 包校验，没有关闭 TLS/签名验证。
首次 QEMU 测试因解包后的 SeaBIOS VGA ROM 搜索路径缺失而失败；
补齐本地固件目录的相对符号链接后，实际启动和 GDB 测试通过。
这些是已解决的环境问题，没有为绕过问题而修改内核逻辑。

## 执行与结果

在仓库根目录实际执行：

```sh
make clean
test ! -e build
make -j2
make
PATH=/workspace/.liberums-tools/bin:$PATH make test
git diff --check
```

| 检查 | 结果与证据 |
| --- | --- |
| 清理 | 通过；`build/` 被移除，随后从源码完整重建 |
| 构建 | 通过；GCC 警告按错误处理，生成 `build/liberums.elf` 和链接映射 |
| 增量构建 | 通过；第二次 `make` 报告无需重建 |
| ELF/Multiboot | 通过；ELF32/i386 EXEC，入口 `0x00100010`，头位于文件偏移 `0x1000`，校验和正确 |
| 链接检查 | 通过；无未解析符号，无动态链接段，无同时标为可写和可执行的 LOAD 段 |
| `make run` | 通过；实际运行 QEMU，三行串口信息全部匹配 |
| `make debug` + GDB | 通过；实际连接暂停的 QEMU，硬件断点命中汇编与 C 入口 |
| GDB detach 后启动 | 通过；继续运行后再次观察到完整三行串口信息 |
| 差异空白检查 | 通过；`git diff --check` 返回 0 |

自动测试终端输出：

```text
PASS: ELF32/i386, entry 0x100010, Multiboot at file offset 0x1000
PASS: make run emitted all three expected serial lines
PASS: make debug + GDB verified _start and kernel_main; boot resumed successfully
```

实际串口内容：

```text
Liberums: boot successful
Phase 1: x86 32-bit C11 kernel, COM1 ready
Multiboot handoff accepted; interrupts disabled; halting.
```

GDB 验证结果：

```text
_start:      EIP=0x00100010 EAX=0x2badb002 EBX=0x00009500
kernel_main: EIP=0x00100060 ESP=0x00105fec
             CS=0x8 DS=0x10 SS=0x10 CR0=0x11 EFLAGS=0x12
stack:       0x00100051 0x2badb002 0x00009500
```

在 C 第一条指令前验证：CR0.PE=1、CR0.PG=0、IF=0、DF=0，
ESP 位于预留栈中且模 16 等于 12，栈上两个参数与加载器交接值相同。
上面的地址随之后源码或工具链变化可能改变，应重新运行测试，不能作为固定 ABI。

当前运行的日志保存在 `build/boot-test.log`、`build/gdb-test.log`、
`build/debug-serial.log`。测试在得到成功证据后主动发送 SIGTERM 关闭自己启动的 QEMU；
日志末尾的终止信息是测试清理，不是 guest 自行关机，也不是以超时判定成功。

## 已知限制与未执行项

- 当前仅是单域 ring 0 内核，未实现分页、用户态、资源所有权或隔离。
- 没有 IDT、异常/NMI 处理、定时器或调度，保持可屏蔽中断关闭。
- 只检查 Multiboot magic 和信息地址非零，不解析/分配内存，不支持任意加载协议。
- COM1 依赖 QEMU 的兼容 UART，使用无超时轮询；不存在设备或设备失效时可能停留在发送等待。
- 没有磁盘引导扇区、GRUB ISO、UEFI 或真实硬件测试；本阶段目标为 QEMU Multiboot 直接加载。
- 未实现 LibOS、文件系统、网络或通用设备管理策略；这些没有被记作通过的测试。
- 已执行的 Phase 1 必需验收无剩余阻塞；完成后停止，没有开展下一阶段实现。
