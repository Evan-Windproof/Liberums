# 8. VGA 与键盘保护

VGA 文本页在物理 `0xb8000`，是 MMIO，不是可分配 RAM；4000 字节字符屏幕占一个 4096 字节设备页。
每字符两字节：字符与颜色属性。用户通过固定 VA `0x09400000` 绘制，内核只验证授权/映射与光标请求。

```text
INFO (discover) -> trusted boot grant -> ACQUIRE -> VGA MAP / KBD POLL -> RELEASE
```

[device_grants.c](../../kernel/device_grants.c)、[vga_device.c](../../kernel/vga_device.c)、
[keyboard.c](../../kernel/keyboard.c)、[ps2.c](../../arch/x86/ps2.c)。
INFO 或 ELF Note 都不能授予权限；VGA 与键盘分别独占，句柄也绑定 Epoch/Generation。

```c
/* 授权后，颜色与布局仍由 Ring 3 选择。 */
((volatile uint16_t *)VGA_USER_ADDRESS)[0] = 0x1f41; /* white A on blue */
```

实验：`make run-teaching-console QEMU_DISPLAY=gtk`（本地图形环境），
或 `make test-libos-examples`（云端 QMP 注入真实模拟 PS/2 IRQ1并检查显存/截图）。
常见错误：把扫描码当 ASCII、忘记处理 break/overflow、让用户任意 in/out 端口。
局限：只针对 PC VGA 80×25、PS/2 translated set 1；队列 64 字节，溢出重置批次；无鼠标、Unicode 终端、GUI、设备共享或多 Console 同屏。
