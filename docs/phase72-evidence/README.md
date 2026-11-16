# Phase 7.2 实际验收

2026-11-16，Liberums2，GCC 14.2.0、Binutils 2.44、QEMU 10.0.13、GDB 16.3。
执行目录 `/workspace/Liberums`，PATH 包含 `/workspace/.liberums-tools/bin`。

- `baseline-phase1-71.log`：修改前 Phase 1～7.1 的原完整回归，退出 0。
- `final-build.log`、`final-regression.log`：最终构建、Phase 1～7.2 回归，退出 0。
- `keyboard-diag-*`：真实 PS/2 IRQ1、EOI、unowned 丢弃及 PIC 状态。
- `keyboard-*-gdb.log` / `*-serial.log`：8 个用户 guests、16 lifecycles，包含实际扫描码、
  Ring 3 编辑/命令/显存、GP/段/ESP/CR3/IRET frame、IRQ0 preemption、授权与清理。
  in60/out64/INT21/#UD 的预期 USER FAULT 由 GDB vector/error 检查判定，不是忽略测试失败。
- `keyboard-functional.png`、`keyboard-overflow.png`：QEMU 原生 screendump；
  不是把串口文本重新绘制成图片。云端没有 GTK，本地实体键盘交互不在此次自动化结论中。
- `overflow-stress.log` / `keyboard-overflow-stress*-gdb.log`：调试检查点修正后额外四次真实 overflow guest，退出 0。
- `capture-diagnosis.log`：修正前的一次探查命令，先有通过后有失败，整体退出 1。
  最后一例出现 GDB 第一 IRQ 指令断点重复计数：165 captures、164 实际 dispatch/EOI/returns。
  这是历史诊断记录，不能算作最终通过。检查点移至首个 PUSH 后，增加与 dispatch 的逐次匹配，
  保留全部现场/返回断言；最终回归和四次压力复验通过。详见教学文档。
- `aelthuvor-compat.log`：复用原 Phase 7.1 独立 compatibility runner，15 guests / 30 lifecycles，退出 0。
  `aelthuvor-elf-manifest.json` 是当前运行前后不变的旧 ELF hash，与 Phase 7.1 记录一致。
  Aelthuvor 源码未改，不声称旧 `test-libos` SDK drift 入口已修复。
- `manifest.json`：基准 SHA、命令与退出码、当前源码/内核/图片 hash、原始日志 hash。

文本导出仅规范 LF/行尾空白，未删除失败断言或修改实际结果。
完整机制、边界、启动方式见 [教学文档](../phase7-keyboard-console.md)。
