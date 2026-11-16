# Phase 7.1 实际验收记录

日期：2026-11-16。工具和完整解释见 [教学文档](../phase7-vga-console.md)。

- `baseline-phase1-6.log`：修改前原 `make test`，退出 0。
- `final-build.log`、`final-regression.log`：最终构建及完整回归，退出 0。
  包含所有原 Phase 1～6、新 native authority 边界、11 VGA 用户 guests / 22 lifecycles、
  独立 Ring 0 诊断、截图 guest，以及两个新增 Makefile 启动入口的功能请求。
- `vga-*-gdb.log` / `vga-*-serial.log`：当前实际 VGA PTE、CPL3、句柄代数、
  readonly、越界、原始 PA、kernel/页表/I/O、主动 release、强制 revoke、OOM 与退出/异常清理。
  peer 的 USER FAULT 是故意执行的无授权访问，成功由 GDB 的 vector/error/CR2 检查决定。
- `vga-console.png`：QEMU QMP screendump 的原生截图；`vga-screenshot.json` 记录 PPM hash 和尺寸。
  没有用串口内容生成或重绘截图。本机 QEMU 只有 `none` 后端；GTK/WSLg 窗口未测试。
- `aelthuvor-compat.log`：未修改旧 Aelthuvor ELF 的 15 guests / 30 lifecycles，退出 0。
  `aelthuvor-elf-manifest.json` 记录源码/SDK 固定 commit 和测试前后不变的 ELF hash。
  此项不是对旧 `test-libos` 字节漂移入口成功的声明。
- `manifest.json`：本次基准、命令退出状态、最终内核 ELF 和源码 hash、原始日志 hash。
  文本导出仅规范 LF 和行尾空白，不改断言结果；实际命令在仓库根目录执行，工具 PATH 为
  `/workspace/.liberums-tools/bin:$PATH`。

ELF 的 debug 路径和工具链会影响 hash，不承诺跨构建目录 bit-identical。
发布 cloud 环境、GTK 本地窗口和后续 Phase 7 功能不属于这些自动化检查的结论。
