# Phase 9：安全、可靠性与 ABI 稳定

开发基准为 Liberums `449c78f9b12535cded2bca575cd44e0b65a0953b`；
开始时工作分支 `work`、HEAD 与远程 main 一致，工作区干净。
先阅读域池、调度、ELF、资源、Capability、撤销、设备、syscall、分页和 Phase 8 文档，
执行修改前完整 `make test`，退出 0 后才修改内核。

本阶段保持四域、八个 boot 模块、16 项普通 RAM、每域四项、每资源四绑定、八项撤销事件及单 CPU。
Kernel ABI 继续 1.2，六个公开 ABI 头文件字节与基准完全相同；未新增 syscall、feature、ELF Note 或进程服务。

## 实际修改

- `kernel/syscall.c`：KBD_RELEASE 的 EDX 清零，兑现原键盘头文件已声明的错误输出契约。
- `tests/phase9_runtime.c`、私有头及 `tests/runtime_checks.c`：显式 selftest 的单次可信撤销请求；
  检查目标 ID/Epoch，调用原 revoke_request/POLL/INFO；不读取 LibOS 数据结构。
- `tests/fixtures/security9/`：ABI 反例和从独立 Aelthuvor Application SDK 构建的 RAM 压力消费者。
- `tests/phase9.py`：真实 QEMU/GDB 的身份路由、诊断授权、POLL/INFO/ACK、强制撤销和故障隔离。
- `tests/revoke_tests.c`：补充 deadline−1 与 deadline 的纯协议边界。
- `tests/abi_contract_tests.c`：冻结 26 个调用号、布局、版本及 profile 兼容规则。
- `tests/phase8.py`：压力轮数参数化；原默认 24 轮、原名称及所有断言保持，Phase 9 调用 128 轮。

没有重写域、资源、分页、调度器或 ELF parser，没有将分配策略、Console 或应用服务移入 Ring 0。

## 复现入口

```sh
make -j2
make test                       # 原 Phase 1～8 + 不依赖外部仓库的 Phase 9
make test-phase9                 # 5 个真实 QEMU/GDB guest，含 128 轮复用
# 独立 Aelthuvor Phase 4 先按自身 Makefile 构建 app-sdk、independent-libos：
make test-phase9-e2e PHASE9_AEL_BUILD=/path/to/Aelthuvor/build
```

E2E 目标只读取已经导出的 SDK，将消费者构建到 Liberums build；不会构建/修改外部源码或 SDK。
核心测试无需安装 Aelthuvor。原 run、run-console、run-external-console、run-multi 行为保留。
详细结果、限制与来源见 [验收](phase9-validation.md)、[安全审查](phase9-security-audit.md)、
[ABI 契约](phase9-abi-contract.md)、[撤销](phase9-resource-revocation.md)、
[身份](phase9-domain-identity.md)、[不同 LibOS](phase9-libos-compatibility.md)、
[架构边界](phase9-boundary-review.md)。本阶段结束后停止，不进入 Phase 10。
