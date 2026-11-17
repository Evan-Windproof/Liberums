# Maintainer Guide

Phase 10 是最后一个主要功能阶段。继续接受安全/Bug/兼容性修复与教学更新，
不自动发展 Phase 11、创建派生系统、归档仓库或删除历史。

## 常见命令与模块

```sh
make clean
make -j2
make test
make sdk libos-examples
make test-libos-examples
make test-phase8
make test-phase9
make test-phase9-e2e PHASE9_AEL_BUILD=/path/to/Aelthuvor/build
make test-libos-compat PHASE10_AEL_BUILD=/path/to/Aelthuvor/build
python3 scripts/check_docs.py
```

boot/异常/分页在 arch/x86/；域身份/资源/revoke/grant 在 kernel/；
公开契约在 include/abi/；教学用户代码在 examples/libos/；实验钩子和硬件审计在 tests/。
[架构课程](architecture-guide/README.md)、[模块历史](history.md) 帮助定位。

## 修复流程

1. 记录基准 SHA、复现步骤、错误状态和保护影响，先分类 P0/P1/P2。
2. 建立真实失败测试，分清工具问题、用户策略限制和内核缺陷。
3. 最小修复，保留旧安全断言；不能改成功判定掩盖失败。
4. 跑受影响测试与完整回归；核对实际 CPL/CR3/PTE/owner、回收和 peer 进度。
5. 更新文档/Changelog，检查 SPDX/来源与 `git diff --check`，审阅后正常提交。

## ABI 与第三方报告

ABI 1.2 保持 major/minor/编号/寄存器/Note/错误契约；设备是可选扩展。
任何新保证先评审版本和能力发现，不偷改旧语义、不修改旧 ELF Note 伪装兼容。
SDK 只从 include/abi 导出，不能手改生成副本。[完整契约](phase9-abi-contract.md)。

兼容报告请提供：Kernel/用户 ELF SHA、源码/SDK commit、工具链、profile/boot grant、
可独立重建的最小项目、日志和期望/实际状态。不要要求报告者先读懂 Aelthuvor。
不要在内核加入特定 LibOS 名称、metadata、allocator 或应用命令分支。

## 发布补丁与派生

先查看远程 main、现有 tag/release 和工作区；版本由维护者确认。
完整验证入口可运行：

```sh
make validate-release VALIDATION_ARGS='--aelthuvor-build /path/to/P4/build --historical-checkouts /path/to/P3 /path/to/P2 /path/to/ABI11'
```

该入口记录真正的命令退出码、源码输入、工具链和内核 SHA；不伪造未执行矩阵。
完成审阅、提交并推送源码后，`make release RELEASE_COMMIT=<commit> RELEASE_VERSION=<version>`
只从固定 Git 对象准备 source/SDK/examples、验证报告、带来源的 ELF、SHA256SUMS。
它拒绝未通过的验证或与 commit 不匹配的受测输入；不会自动发布 GitHub Release。
检查归档/校验，再创建新的 annotated tag 并正常推送；禁止覆盖旧 tag 或强推。
有 API 权限时用 `gh release create <tag> --verify-tag --notes-file RELEASE-NOTES.md build/release/*`，
权限不足则交付文件并明确未发布，不绕过权限。

未来派生系统应从已验证 tag 新建**独立仓库/分支**，保留 GPL 和来源；
不要重写 Liberums 历史、悄悄改其 ABI 或把派生功能并回教学基线。
