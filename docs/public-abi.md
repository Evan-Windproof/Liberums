# Public Kernel ABI SDK

唯一权威定义是 [include/abi](../include/abi/)，当前 ABI **1.2**。
`make sdk` 直接导出六个头、LICENSE、README 与 manifest 到 build/kernel-sdk；
`manifest.json` 保存每个头的 SHA256、来源 commit（无 Git 的源码包会明确为 null）和导出方式。
没有单独维护的第二套常量。`--revision COMMIT` 可从固定 Git 对象导出用于发布的 SDK。

```sh
python3 scripts/export_kernel_sdk.py --output /tmp/public-kernel-sdk
make -C examples/libos/minimal SDK=/tmp/public-kernel-sdk
```

用户只传整数：EAX=调用号，EBX/ECX/EDX/ESI=参数，EAX=有符号状态/值，EDX=约定的额外输出。
SDK 的 extra 指针在用户态使用，内核不解引用它。错误先检查再转 unsigned。
完整 26 调用的输入、输出、权限、状态变化、错误及 profile 要求见
[Phase 9 契约表](phase9-abi-contract.md)，不在这里复制另一份易漂移的表。

| 家族 | 内容 |
|---|---|
| 0～4 | YIELD、EXIT、受限 LOG、ABI 查询、配额查询 |
| 16～23 | CLAIM/SPECIFIC、MAP/UNMAP、RELEASE/QUERY、TRANSFER/RECEIVE |
| 24～26 | REVOKE POLL/INFO/ACK；没有公开 REQUEST |
| 32～36 | 可选 VGA，固定 MMIO 与光标 |
| 40～44 | 可选键盘、原始扫描码与统计 |

Major 必须一致、kernel minor 至少为所需 minor，features 必须包含全部要求。
合作 profile=31，抢占 profile=119；不要只看版本号推断所有功能存在。
可选设备用 INFO 探测，但探测不授予权限，ELF Note 也不是 grant。
旧 ABI 1.1/原 1.2、原 36 字节 Note、错误和调用号保持；本阶段没有升级 ABI。

LibOS Application API（例如本例 libos_main，或外部 SDK 的 ael_*）属于用户态设计，
不是 Liberums 必须提供的新 syscall。你可以只用 Kernel SDK 从零写自己的 API。
