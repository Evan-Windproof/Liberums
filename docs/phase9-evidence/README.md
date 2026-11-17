# Phase 9 证据索引

日期 2026-11-17，i686/TCG/单 CPU/32 MiB。文件来自本阶段实际执行，没有把旧成功日志当新测试。

- `baseline.log`、`regression.log`：修改前基线与修改后完整 make test，均退出 0。
- `abi-contract-before-failure.log`：旧 KBD_RELEASE EDX 反例；对应修复后 GDB 日志单独保留。
- `e2e-target.log`、`phase9-results-{core,e2e}.json`：实际 Makefile 专项入口，core 五项包含在 E2E 21 项内。
- `phase8-reuse-128-*`：128 次真实 EXIT/#UD 复用的逐轮身份、资源/物理页/回收记录。
- `phase8-phase9-*-gdb.log`、`*-serial.log`：真实请求、协议 trace、PTE/物理零化、#PF 与隔离检查。
- `ael4-*.log`、`ael3-*.log`、`ael2.log`、`abi11.log`：外部各固定版本的实际兼容完成日志。
- `ael4-results.json`、`ael4-build-results.json`：外部 SDK/Reference 独立构建、不同策略/故障与内核 SHA 不变。
- `native.log`：宿主模拟边界，不能与真实 QEMU 数量混算。
- `provenance.json`：基准、最终 ELF SHA、公开头字节一致性、外部 checkout/SDK/ELF 哈希与干净状态。

文本归档统一 LF、移除行尾空白，数值和结果不改；原始串口 CRLF 在忽略的 build 中。
不提交 ELF、对象、SDK 副本、工具链、缓存或第三方源码。
本阶段未增加 GTK 证据；原 renderer/显存/PS2 回归与本地窗口验证是不同结论。
运行方式、完整断言和限制见 [验收文档](../phase9-validation.md)。
