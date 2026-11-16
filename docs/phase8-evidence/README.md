# Phase 8 验收证据

2026-11-16，QEMU 10.0.13/TCG/i686/单 CPU/32 MiB，GDB 16.3，GCC 14.2.0。

- `regression-passed.log`：最终完整 `make test`，退出 0，原 Phase 1～7.3 与新增 Phase 8 均执行。
- `standalone-passed.log` / `phase8-results-all.json`：专项退出 0；19 QEMU guests，18 GDB audits + 实际 run-multi。
- `baseline-initial-failure.log` / `baseline-passed.log`：旧 Phase 4 停点观察失败与修正后旧基准通过。
- `regression-observer-failure.log`：旧 Phase 7.2 重复 IRQ1 debugger stop 的失败记录；最终通过单独保留。
- `phase8-*-gdb.log` / `*-serial.log`：真实地址、CPL、CR3、TSS、IRQ/EOI、Epoch、资源及回收证据。
  `reuse-24` 包含全部 24 轮的实际资源/终止/回收记录，不能只据无 Panic 判定通过。
- `native.log`：真实域池实现的宿主 ASan/UBSan 测试（ELF/硬件/RAM 端点模拟），不算 QEMU。
- `ael-phase3-{memory,console}.log` / `ael-phase2.log` / `ael-abi11.log`：未修改外部仓库的独立集成完成记录。
- `ael-coexist.log` / `phase8-ael-results.json`：三类 Aelthuvor + 三 worker 的四域实验及独立 ELF 前后哈希。
- `phase71-compat.json`、`phase73-results.json`：历史兼容及原 7.3 runner 的实际计数，不与重跑叠加。
- `ael-console-four-domains.png`：QEMU screendump，在四个域并存时，真实 PS/2 输入经 Ring3 完成 help/status/memtest 后截图。

归档文本统一为 LF 并去除行尾空白；未改动测试值和结论，原始串口 CRLF 日志仍在 build。
生成的 ELF、对象、SDK 副本、工具链和缓存均留在 Git 忽略的 build 或外部临时目录；不提交。
以上截图是模拟 VGA 渲染，不是云端 GTK 窗口。本地 WSLg/实体键盘需维护者按教学文档验证。
命令、断言范围、观察器修正及限制见 [验收文档](../phase8-validation.md)。
