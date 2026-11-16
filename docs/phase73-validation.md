# Phase 7.3 验收记录

开发基准：Liberums `b24695da32f147d6de107e2e60bbbbc81a2f4d68`，开始时与远程 main 相同。
环境：恢复的 Liberums2、i686 pc/TCG、单 CPU/32 MiB、GCC 14.2.0、Binutils 2.44、QEMU 10.0.13、GDB 16.3。
日期：2026-11-16；记录和截图见 [phase73-evidence](phase73-evidence/README.md)。

## 复验命令

```sh
export PATH=/workspace/.liberums-tools/bin:$PATH  # 仅此云环境
make -j2
make test
make test-external-console
python3 tests/phase73.py --aelthuvor /tmp/liberums-phase73-ael
make test-vga-compat AELTHUVOR=/tmp/liberums-phase71-ael
# 对未修改、已经构建的 Aelthuvor Phase 2 checkout：
make -C /tmp/liberums-phase73-ael integration LIBERUMS=/workspace/Liberums \
    QEMU=qemu-system-i386 GDB=gdb
```

路径需替换为相应固定版本 checkout；不要求创建 sibling repo 才能构建/运行 Liberums。
`make test` 包含 Phase 1～7.3；专项 native suite 和独立 ELF 构建不算 QEMU guests。
精确 guest/lifetime 数从本次 `phase73-results.json` 与各 runner 完成记录导出，
不把重复命令合并为虚构的独立覆盖率。

## 本次结果

- 原 Phase 1～7.2 修改前基准退出 0；完成后的完整 `make test` 退出 0。
- 带 Phase 2 Aelthuvor 检查的专项：22 个 QEMU guest，36 个完成回收的 domain lifetimes。
  其中 3 个生产持续运行会话和 1 个启动检查由测试宿主停止，不算完成回收。
- 历史 Aelthuvor suite：15 guests / 30 lifecycles，退出 0。
- 当前 Aelthuvor Phase 2 自有 integration：13 guests / 26 lifecycles，退出 0，
  包括 First-fit/Best-fit、两种非合作 CPU profile 和 ABI 1.1 历史映像。
- `git diff --check` 通过；两个 Aelthuvor checkout 的 tracked status 保持干净。

这些是不同 runner 的实际范围，不将多次运行的同一 suite 相加作为覆盖率。

## 自动化检查

| 范围 | 实际检查 |
|---|---|
| 独立性 | 仅复制用户源码/公开 SDK，修改入口及 editor 后重建，内核 SHA/mtime 不变；实际显示修改结果 |
| 替换 | 原 Console、Amber、修改后的 ELF 使用同一内核/profile；无 filename/entry/role 特判 |
| ELF | PT_LOAD/PT_NOTE、逐字节 file/BSS、RX/R/RW、跨页布局、真实 CPL3/CR3/私有 stack |
| 交互 | QMP→PS/2→真实 IRQ1→capability→Ring3→实际 VGA，help/about/status/clear/Backspace/Enter/Shift/Caps/光标 |
| 授权 | 普通 ELF/同名文件/同 ABI note 默认无权限、RAM/伪造/旧句柄拒绝、只读不能提升、epoch/generation 重建 |
| 生命周期 | EXIT/#UD、两设备分别强制 revoke、VGA 撤除后 #PF、队列/IRQ1/页表/RAM 回收 |
| 失败 | 缺模块、文件缺失、坏 ELF/ABI、部分页 OOM、无 VGA、无 PS2、部分用户初始化、native busy/grant 事务回滚 |
| 兼容 | 原测试断言保留；旧 Aelthuvor ABI 1.1 suite 与当前 Phase 2 ABI 1.2 的内存/非合作抢占 suite |

持续运行的生产程序用测试宿主终止 VM，不计作已完成的 guest domain lifetime；
测试变体真正执行 EXIT/FAULT 并到达回收检查点才计入 completed lifetimes。

GDB 在 IRQ1 保存 GP 前及 IRET 前检查原 CPU frame、GP/段/ESP/CR3、dispatch/EOI；
输入按每个实际 POLL 的用户观察序号匹配，不把重复 debugger stop 当新输入。
多轮脚本在 GDB 命令层切换断点，避免在 Python stop callback 内删除当前断点导致 GDB 自身崩溃。
这些是测试工具处理，不删除权限/现场/清理断言，不向内核队列写入伪造键盘数据。

一次全回归发现旧空 `domains_reaped` checkpoint 的第一条 RET 被 GDB 重复报告，
实际回收检查两次读到同一 completed_rounds。域 prepared/reaped 增加 NOP 作为非空观察点，
未改域生命周期或原测试断言；受影响的原 Phase 3 suite 及最终完整回归重新执行。
历史测试工具失败与最终结果分开保留，不能把诊断失败记为通过。

## Aelthuvor 与本地验证限制

Aelthuvor Phase 2 固定到 `1d288be418ddeef89462914769d5ea84bb557ade`，SDK 来源固定 Phase 6。
其 tracked status 保持干净，已用于授权反例的 cooperative/preempt ELF 在后续 integration 前后 hash 相同；
不改 SDK manifest，不推送该仓库。历史 suite 另保留其原有全部 ELF 的前后摘要检查。
现有 First-fit/Best-fit 与非合作 CPU loop/IRQ0 恢复被其自有 integration auditor 检查。
额外从 Liberums 侧检查普通外部启动下的 cooperative/preempt ELF 都没有设备 grants。
不宣称 Aelthuvor 已支持 Console；未来 SDK 建议见 overview。

云端没有 GTK 后端，显存成功、QEMU 渲染截图成功与本地窗口实际交互分开报告。
本地操作步骤见 [独立 Console](phase73-external-console.md)，实体键盘/窗口焦点仍需用户确认。
