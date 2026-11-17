# Lesson 7 — 可选 VGA/PS2

先有可信 boot grant，后有用户 ACQUIRE。Note、INFO、知道端口号都不授予权限。
新 Console 的 runtime、字节解释和文本布局都是用户代码，见
[独立 Console](../../examples/libos/console/README.md)。

```sh
make libos-examples
make run-teaching-console QEMU_DISPLAY=gtk
# 无桌面只跑自动验证，不宣称窗口可见：
make test-libos-examples
```

Generic QEMU 配置使用 `multi-external multi-vga=1 multi-keyboard=1`，Console ELF 放在模块 1。
模块序号不是永久 Domain ID；两个设备的 grant 相互独立且均为独占。
VGA MAP 固定 VA/设备页，字符/颜色用普通用户写入完成；键盘 POLL 返回扫描码，不是 ASCII。
溢出需要重置用户 decoder/editor，处理 make/break、Shift/Caps、Backspace 与 Enter。

`exit` 命令显式 RELEASE 两个句柄，域清理还会撤除 grant、清队列并处理页表。
负例把设备授予模块 2，模块 1 虽可 INFO 仍不能 ACQUIRE。
不允许用户直接 in/out PS2 端口，不实现设备共享、键盘布局框架或多个 Console 同屏。
