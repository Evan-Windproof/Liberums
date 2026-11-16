# Phase 7.3 实际证据

运行目录 `/workspace/Liberums`，恢复的 Liberums2。日期、版本、命令见 [validation](../phase73-validation.md)。

- `baseline.log`：开发前原 Phase 1～7.2 全套，退出 0。
- `final-build.log` / `final-regression.log`：最终构建与 Phase 1～7.3 完整回归，退出 0。
- `final-special.log` / `results.json`：附加 Aelthuvor Phase 2 的专项，22 guests / 36 completed lifetimes，退出 0。
  3 个持续交互生产会话及 1 个启动检查不计入完成回收。此 suite 的构建子进程还证明修改入口/editor 后内核 SHA/mtime 不变。
- `phase73-*-gdb.log` / `*-serial.log`：每个外部程序、设备权限、ELF 布局、实际键盘输入、IRQ1/IRET 与回收。
- 三张 `phase73-*.png`：QEMU 原生 screendump，独立程序不同主题及无需重链接内核的 editor 修改。
  云端没有 GTK；不把显存、渲染器截图与本地窗口/实体键盘等同。
- `ael-phase2-integration.log`：未修改当前 Aelthuvor Phase 2 的自有 integration，13 guests / 26 lifecycles，退出 0。
  固定源码/SDK 与 ELF hashes 见 `ael-phase2-provenance.json`，其仓库未提交或推送。
- `ael-historical.log`：保留的旧 Aelthuvor 审查 runner，15 guests / 30 lifecycles，退出 0。
- `phase3-diagnostic-failure.log`：首轮 full regression 中旧空 RET checkpoint 重复停留，整体退出 2。
  `phase3-recheck.log`：增加 NOP 观察点后，原 Phase 3 所有断言重新通过，退出 0；最终全回归也通过。
- `observer-diagnostic-failure.log`：早期新 GDB observer 的工具计数失败，退出 1。
  最终 observer 使用实际 dispatch ordinal 与用户 POLL 观察序号核对唯一事件、在命令层切换断点，
  保留原始现场、扫描码顺序和清理断言；历史诊断失败不计为最终通过。
- `manifest.json`：基准、退出状态、源码/ELF/PNG hash 与原始日志摘要。

只提交源码、文档、文本日志与三张预期截图；内核/用户 ELF、工具链和构建缓存均保留在忽略目录。
文本导出只规范 LF/行尾空白，不删除失败断言或修改结果。debug 路径会影响 ELF hash，不承诺跨路径 bit-identical。
