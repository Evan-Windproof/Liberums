# Phase 7.3 Exokernel 边界审查

判断依据是保护与管理策略分离，不是按驱动文件/行数/syscall 数量打分。
新公开 syscall 数为零，公开 ABI 仍为 1.2；ELF validator 与外部页构造器保持原实现。

| 重要模块 | 特权级 | 职责 | 为什么放在这里 |
|---|---|---|---|
| `user/console/core.c` / `.h` | Ring 3 | keymap、编辑、主题、布局、滚动、四个命令、光标选择 | 都是用户管理策略，不应进内核 |
| `user/interactive_console.c` | Ring 3 | 保留旧单页实验入口、复用 core | 保持旧回归，外部程序不依赖其固定 DATA |
| `user/external_console/main.c` / Makefile / linker | Ring 3 / 构建工具 | 独立 ELF 入口、SDK 探测、普通 data/BSS、不同主题 | 应用可替换，不链接内核对象 |
| `kernel/external_launch.c` | Ring 0 可信启动 | 将显式 command line 转为有限 grant/profile/lifetime 配置 | 当前没有用户态授权启动器；权限选择必须可信 |
| `kernel/device_grants.c` | Ring 0 | 两个既有设备的事务式 grant 与分别 revoke | 跨域授权、回滚与独占必须由可信侧执行 |
| `kernel/external.c` | Ring 0 | 原 ELF 验证/域建立，加上验证后的 grant 与失败空间回滚 | 不解释程序内容或 Console 状态 |
| `kernel/keyboard.c` 新 abort 入口 | Ring 0 | 回收无 owner 的失败硬件初始化 | PIC/PS2 必须特权操作，防止半初始化泄漏 |
| `kernel/main.c` | Ring 0 | 启动配置、必要探测、选择已有外部 runner | 不含字符、命令或应用循环 |
| `arch/x86/paging.c` | Ring 0 | 新 profile 下匹配的 supervisor VGA alias | 保护页表和缓存属性仍需可信侧 |
| `kernel/scheduler.c` | Ring 0 | 旧调度/终止清理，三个旧实验 hook 合并为一个 dispatcher | 不增加具体外部 Console 特判 |
| `tests/runtime_checks.c` / diagnostics header | Ring 0，仅显式实验 | 旧实验转接、可选 revoke-only 诊断 | 移出 scheduler，普通用户不能调用或授予权限 |
| `kernel/domain.c` | Ring 0 | 两个旧空 checkpoint 增加 NOP | 仅稳定 GDB 观察，不改域管理或测试断言 |
| `user/external_console/probe.c` / `layout.c` | Ring 3 测试 | 不可信句柄攻击、多页 ELF 边界 | 通过同一公开 ABI 受真实保护，无内核权限 |
| `tests/phase73.py` / native suites | 宿主测试 | 真实输入、MMU、IRQs、ELF、回滚和独立构建证明 | 断言不成为内核业务逻辑 |

## 每项新增 Ring 0 功能的六个问题

### 可信配置解析

1. 解析字符串本身不需特权，但选择谁取得设备必须位于当前可信启动链。
2. 只做有限启动授权策略，不做用户态编辑、颜色或 LibOS 管理。
3. 不依赖 Aelthuvor 或任何 ELF 名称/role。
4. 任意合法外部 ELF 均可由相同配置启动，无需改内核。
5. 小幅扩大 TCB：有界 token 到有限 mask 的解析。
6. 没有新增 syscall/ELF note 承诺；这些是实验性启动参数，未来可由受限启动器代替。

### 设备 bundle、回滚与无 owner 硬件 abort

1. grant/revoke、页表/PIC/端口保护必须有特权，用户不能自行仲裁所有权。
2. 不实现输入消费顺序、UI、分配器或应用任务安排。
3. 只复用 VGA/keyboard 两个保护模块；没有 LibOS 数据结构。
4. 对用户程序仅要求公开 ABI，不检查其实现。
5. TCB 新增有限事务顺序；原 capability 检查、IRQ 与 mapping 机制未被绕过。
6. 没有公开委托或通用设备管理 ABI；generation/epoch 和原设备 ABI 语义保持。

### 外部 lifetime 与启动失败回滚

1. 创建/销毁隔离域和收回资源必须可信。
2. 一轮不等于短时任务：用户循环决定交互长度；内核仅在终止时清理。
3. 无 LibOS 初始化策略、Console 控制字段或特殊 entry 比较。
4. 更换 ELF 参数即可更换程序，普通外部双轮模型不变。
5. 新风险在 grant 与空间创建之间的部分失败；native 与 QEMU 缺设备/OOM 验证事务回滚。
6. 原 ABI/ELF profile 无变更，不承诺通用进程模型。

### 早期 VGA alias 与诊断 dispatcher

1. MMIO 缓存/页表以及强制撤销必须可信；dispatcher 只是已有实验入口的归拢。
2. 不解读用户输入或决定显示内容。
3. 不依赖任何特定 LibOS。诊断可选项是 trusted test configuration，不能由 ELF 触发。
4. 正常程序无需诊断字段、GDB 或 debug syscall 即可持续交互。
5. 诊断仍编入实验内核，属于 TCB；新路径仅 revoke，不会给所有外部 ELF 自动授权。
6. 没有新用户 ABI。未来发行构建可另行隔离诊断，但本阶段不引入新构建框架。

## 对三处主要编排代码的结论

- main 只增加可信配置读取/硬件探测和外部路径选择，不知道四条命令、主题或用户变量。
- scheduler 不增加 Phase 7.3 分支；三个旧 hook 调用移到 tests dispatcher，调度顺序不变。
- external runner 不读取文件名/role/Console 全局变量；校验、空间建立、grant、运行、清理可独立审查。

内核确实保留最小可信启动策略，并非所有策略都机械地禁止在 Ring 0；
这里的必要性是授权不能由尚未获得权力的程序自我声明。
不宣称实现 Secure Boot、恶意 bootloader 防护、通用 DMA 隔离或用户态驱动权限委托。
