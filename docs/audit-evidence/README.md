# 本次审查原始证据

这些记录来自本次环境实际执行，不是历史报告摘录。被测内核源码为 `85a493466afe2038ca22a9870f6f9ce37dd7f3a8`，LibOS 为 `b10d337d254b18e98204990203a77a215c82c568`。最终审查提交仅增加 docs 和 `tests/audit_aelthuvor.py`，不会改变被测生产代码。为满足文档 `git diff --check`，归档文本只统一为 LF、移除行尾空白和文件末尾多余空行；没有删除或改写实质输出。原始字节日志保留在本次 work/evidence 或 build 中，其 SHA256 另存清单。

- `manifest.json`：命令退出码、工具参数、实际 ELF SHA256、归档文本 SHA256 和归档前日志字节 SHA256。时间为 UTC，用户本地日期为 Asia/Taipei 2026-11-16。
- `Liberums-make-*.log`、`Aelthuvor-make*.log`：原始构建/宿主/VM 套件输出。原 integration / test-libos **失败**；独立 audit **通过**，不能互相替代状态。
- `original-libos-*.log`：直接运行历史 LibOS GDB suite 的首次失败；保留内层 GDB 原始输出和串口记录，其中显示精确 ABI=1.1 断言失败，外层退出码记录于 manifest。
- `sdk-source-{current,pinned}.log`：当前源目录不满足旧 SDK 精确来源条件；精确旧 worktree 验证通过。
- `phase5-aelthuvor-*-gdb.log` / `-serial.log`：独立审计的九个集成 cases。名称 phase5 是既有测试命名，运行的仍是 **Phase 6 内核**；ABI 返回值为 1.2。
- `audit-aelthuvor-{first,best}-preempt-rejected-*`：未修改 ELF 的抢占 profile 拒绝；不代表抢占执行成功。
- `audit-aelthuvor-{first,best}-peer-{read,write}-*`：实际读写另一域仍存活资源的硬件拒绝；GDB 仅设置 peer 自己用户数据中的现有测试输入。
- `phase6-*-gdb.log` / `-serial.log`：显式 make test-phase6 的寄存器恢复、IRQ、撤销与外部抢占 fixture 记录；外部 fixture **不是 Aelthuvor**。immutable ABI 1.1 fixture 沿用既有 `phase5-phase6-*` label。
- 选取的 `phase3-*`、`phase4-*`、`phase5-oom-*`：域边界、资源 alias/transfer/generation 和加载回滚的原始硬件证据。完整套件每项状态保存在 make test 输出，未把未归档的所有细粒度 GDB 输出都提交到此目录。
- `independent-*.log`：无 sibling 源码的 git archive 导出构建。
- `toolchain-packages.txt`：工作区解包工具的精确 Debian 包名/版本/架构。未更改系统包配置。

GDB 生成的 `.gdb` 脚本在 build 中，有些嵌入被审 ELF 的完整 bytes，因此未全部随文档复制；可由仓库现有测试和新增审计脚本重建。这里保留的是实际运行输出。宿主 mock 断言与 QEMU 硬件断言的区别在报告中单列。调试器改输入/边界测试注入、预期用户故障、预期拒绝也不应混同于普通应用运行。

复现命令、已知失败、未运行项和能力边界见 [集成报告](../aelthuvor-integration-audit.md)，风险与实现位置见 [架构报告](../exokernel-architecture-audit.md)。
