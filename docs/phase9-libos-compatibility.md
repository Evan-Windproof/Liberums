# Phase 9：独立 LibOS 兼容性

内核没有 Aelthuvor 名称、ael_* 调用或 Reference 对象池分支。
所有测试经相同公开 Kernel ABI、ELF parser、CR3 与资源保护运行。

## 固定外部来源与实际结果

| 外部实现／版本 | 公开依赖及策略 | 实际验收 |
|---|---|---|
| 历史 Aelthuvor ABI 1.1 | 原固定 SDK/manifest 与 ELF；历史版本查询期望由既有兼容 runner 适配 | 15 guests / 30 lifecycles，源与 ELF 不变 |
| Aelthuvor Phase 2 `1d288be418ddeef89462914769d5ea84bb557ade` | Kernel ABI 1.2，first/best fit、合作/抢占 | 自有 integration 13 guests / 26 lifecycles |
| Aelthuvor Phase 3 `46c7284325cdb04f7f6fd6a96377cf8ab9721a67` | 同 ABI；Heap、Console、VGA/PS2 | memory 13/26；Console 9 guests、11 完成 lifetimes + 1 持续会话 |
| Aelthuvor Phase 4 `297d7a3e2852b495b66f4a2b585b461e1bcc28b7` | Application SDK 1.0，不等于 Kernel ABI 版本；opaque context、可选设备 | Phase 4 integration 13 guests / 18 lifecycles，包含 Hello、独立编辑的 app、两种 Memory、SDK Console、profile/泄漏/故障反例；原 memory 13/26、Console 9 guests/11 完成 + 1 持续会话也通过 |
| 同一外部仓库的 Independent Reference | 自己的 _start、runtime、syscall wrapper、linker、64×64 字节固定对象栈；仅依赖公开 Kernel ABI，无 libael/ael_* | 独立导出目录重建与运行、两 LibOS 并存、他域句柄拒绝、Reference fault 后 Ael app 继续；原作者的独立集成审计保留 |
| 新 RAM E2E 消费者 | 只链接未修改的 Ael exported SDK，first/best 各自构建；自身源码在 Liberums tests | 16 个四域撤销 guests；每次都有未修改的 Independent Reference 正常退出与两个 worker；合计专项 21 guests 含核心五项 |

两个 Reference 与 Ael 的实现策略不同：前者固定对象/LIFO 复用，后者可变大小 first/best fit；
它们不共享应用 SDK、不共享映射或未授权资源。来自同一个外部源码仓库不代表依赖相同 LibOS 实现。
独立构建审计检查依赖文件和链接 map 中没有 Ael 内部头、静态库、内核私有头或未定义符号。

运行外部仓库自己的测试只创建其忽略的 build 产物或仓库外临时消费者。
源码、原 SDK、manifest、Git 历史及远程均未修改；所有 checkout 的 tracked status 在测试前后干净。
没有复制或重新许可 Reference 到内核。原许可证 GPL-3.0-or-later 保持。

## 复现（路径替换为独立 checkout）

```sh
make -C /path/to/Aelthuvor app-sdk apps independent-libos phase4-fixtures
make -C /path/to/Aelthuvor phase4-integration LIBERUMS="$PWD"
make -C /path/to/Aelthuvor integration LIBERUMS="$PWD"
make -C /path/to/Aelthuvor console-integration LIBERUMS="$PWD"
make test-phase9-e2e PHASE9_AEL_BUILD=/path/to/Aelthuvor/build
```

历史兼容需要相应版本 checkout，不能改 Note、SDK manifest 或旧 ELF 使之假装兼容。
Phase 4 runner 的历史文字仍可能称“unchanged Phase 8 kernel”；本次实际测试二进制以
归档的 Kernel ELF SHA256、工作树变更和最终交付提交为准，未修改该外部脚本的文字。

## Phase 10 中立教学建议

以公开 ABI 为起点，不要求第三方先读完整 Aelthuvor：

1. Hello：最小 _start、linker、36 字节 Note、QUERY、LOG、EXIT。
2. 自己的 wrapper 和 runtime：解释寄存器、profile 与错误，不必使用同一种 Application SDK。
3. CLAIM/MAP/RELEASE、句柄与配额；实现一个固定对象池再比较可变大小策略。
4. 学习 POLL/INFO/ACK、空闲页主动归还、活跃内存安全失败与旧指针 #PF。
5. 通过 GDB 核对两域 CR3/PTE/owner，再加入可选设备与显式 grant。

未来值得评估纳入教学示例的是 Reference 的小型 start/runtime/syscall/pool/linker 和独立消费者，
必须保留 GPL 声明、来源固定版本、归属与清晰的教学维护责任；Phase 9 尚未搬入。
Ael allocator、App SDK、Console 主题/命令和其完整应用框架应继续留在外部仓库。
可同时展示至少两种自建策略，避免把任何第三方 LibOS 定为内核的唯一实现。
