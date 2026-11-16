# Phase 7.3：External Console & LibOS Integration

原则保持 **Kernel protects resources; LibOS manages resources.**
本阶段把 Phase 7.2 的 Console 策略提取到共享用户态模块，并以独立 ELF32 运行。
不修改 Aelthuvor，不增加 Shell/文件系统/网络/GUI/IPC/动态链接器或通用驱动服务。

## 阅读顺序

1. [独立构建与替换程序](phase73-external-console.md)
2. [可信设备授权与回滚](phase73-device-grants.md)
3. [实际验收与限制](phase73-validation.md)
4. [Exokernel 边界审查](exokernel-boundary-review.md)

## 三条路径

| 命令 | 用户程序 | 生命周期与设备权限 |
|---|---|---|
| `make run` | 原 Phase 1～6 内置实验 | 原默认串口/headless 行为 |
| `make run-vga` / `make run-console` | 原 Phase 7.1 / 7.2 内置映像 | 原测试启动器与回归保持 |
| `make run-external-console` | `CONSOLE_ELF` 指定的普通 ELF32 | 可信 profile 授权；程序自己选择是否持续运行 |

原普通 `external` 路径仍默认运行两轮。新的 `external-console` profile 默认为一轮：
一轮可以包含任意长的用户交互，直到程序 EXIT 或 FAULT；内核没有 Console 专属无限循环。
`external-repeat` 是显式双轮验收配置，用户程序每轮仍须自行退出/故障。

Console 的 keymap、编辑、滚动、颜色、布局和四条命令都在 `user/console/core.c`。
外部入口 `user/external_console/main.c` 使用普通 ELF data/BSS 与公开 ABI，不使用内核头、
固定 USER_DATA、内核全局变量或 `domain->space.pages[]`。内置入口保留旧 fixture 数据接口，
只在旧路径使用，外部程序不包含它。

两个独立程序 `console.elf` 与 `amber.elf` 使用同一设备 ABI、不同用户态欢迎文字/配色。
内核不认识它们的文件名或角色。ELF Note 只声明版本/features，不能授予设备权限。

## 与 Aelthuvor 的关系

当前 Aelthuvor Phase 2 的远程基准为 `1d288be418ddeef89462914769d5ea84bb557ade`，
SDK 固定 Liberums Phase 6 `85a493466afe2038ca22a9870f6f9ce37dd7f3a8` 的 ABI 1.2。
其 SDK 尚无 VGA/keyboard 可选头；本阶段不覆盖其 manifest，不宣称它支持 Console。

公开设备接口只传标量、资源句柄与 generation。其他 LibOS 可在自己的 SDK 更新任务中：
核对并固定 `vga.h` / `keyboard.h` 及依赖摘要；保留旧 ABI；探测 INFO；
选择支持 IRQ0 的运行 profile；在用户态封装句柄、解码与绘制策略。
设备授权仍应来自可信启动配置，不能把 LibOS ELF Note 当作 capability。

这里不实现用户态启动器或权限委托。独立 Console 是证明公开 ABI 足够使用设备的用户程序，
不是把 Console 合并进 Aelthuvor，也不是让内核理解某个 LibOS 的运行时。
