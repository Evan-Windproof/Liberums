# 实验性公开 ABI 1.2 与最小 SDK（含 1.0/1.1 兼容约定）

## SDK 的边界

外部程序仅使用 `include/abi/`，可将该目录完整复制到独立项目的 include 路径。

| 头文件 | 公开内容 |
| --- | --- |
| abi/syscall.h | 原调用号、ABI 查询、版本/特性位、整数寄存器调用封装 |
| abi/resource.h | Phase 4 调用号、句柄、权限、错误码、动态窗口及 4096 字节页大小 |
| abi/event.h | 1.2 撤销号 24..26、事件字段与原因 |
| abi/elf.h | 映像/栈范围、预算、ELF ABI note 的公开格式 |

原 kernel/user_abi.h 和 resource_abi.h 保留内置测试布局/模式定义，并引用公开常量，
没有复制出两套可漂移的调用号。kernel/capability.h 继续保留私有授权记录，句柄结构移至公开头文件。

## ABI 查询与兼容性

`SYS_ABI_QUERY=3`，不接收参数：EAX 返回 `(major << 16) | minor`，EDX 返回 Feature Flags。
当前版本为 `0x00010002`。默认兼容 profile 的 flags 为 `0x1f`；显式抢占 profile 为 `0x77`。

| 特性位 | 名称 | 当前含义 |
| --- | --- | --- |
| 1 | PAGES | Phase 4 资源申请/绑定/释放 |
| 2 | TRANSFER | 受限独占转移与接收通知 |
| 4 | ELF32 | 本文规定的受限 ET_EXEC 加载 profile |
| 8 | COOPERATIVE | 单 CPU 合作式用户执行，无 CPU 抢占保证 |
| 16 | LIMITS | 只读资源额度查询（1.1 新增） |
| 32 | PREEMPTION | 当前执行 profile 使用 PIT 强制用户抢占（1.2） |
| 64 | REVOCATION | 当前 profile 支持普通资源页的有界通知/ACK/强制撤销（1.2） |

`liberums_abi_compatible(version, features, major, minor, required)` 要求 major 相同、
实际 minor 不低于所需值、全部 required 特性存在。客户端不应因为多了一个 minor 就拒绝运行。

兼容规则：保留既有调用号、已定义参数/返回语义、错误码和权限位；新增兼容功能应使用新调用号/
特性位并推进 minor。破坏这些约定或缩减既有地址 profile 需要新 major。
当前是实验接口，不承诺永久稳定；独立项目应记录使用的 Liberums 提交及 ABI 要求。
ELF note 让加载器提前检查要求，程序启动后仍应查询实际 ABI。note 不是信任或授权证书。

## 寄存器调用约定

调用方式为 `int $0x80`；EAX=调用号，EBX/ECX/EDX/ESI 为整数参数。
内核不接收字符串或用户指针。EAX 是结果，另有输出时使用 EDX；其他通用寄存器保持。
旧调用的行为不变：

| 号 | 操作 | 输入 / 输出 |
| --- | --- | --- |
| 0 | yield | 参数忽略；重新运行时 EAX=0 |
| 1 | exit | EBX=uint32 退出状态；不返回 |
| 2 | log_char | EBX=0..255；0 成功，-22 越界，-11 超过每域 128 次额度 |
| 3 | ABI query | EAX=版本，EDX=特性位；无输入 |
| 4 | resource limits | 无输入；EAX=每域动态页上限，EDX=每资源绑定上限 |
| 16 | page_claim | EBX=rights；EAX=index，EDX=generation |
| 17 | page_claim_specific | EBX=PFN，ECX=rights；EAX=index，EDX=generation |
| 18 | page_map | EBX=index，ECX=generation，EDX=VA，ESI=R/W；EAX=0 |
| 19 | page_unmap | EBX=VA；仅当前域，EAX=0 |
| 20 | page_release | EBX=index，ECX=generation；EAX=0 |
| 21 | page_query | EBX=index，ECX=generation；EAX=PFN，EDX=allowed rights |
| 22 | page_transfer | EBX=index，ECX=generation，EDX=目标 ID，ESI=新 rights；EAX=0 |
| 23 | page_receive | 无；EAX=index，EDX=generation |

claim/specific/receive/query 失败时 EDX=0。其他资源调用不把 EDX 当作返回值。
SDK `liberums_call()` 的最后一个指针只在用户本地接收寄存器输出，不传给内核。
未知调用 EAX=-38；错误使用 32 位补码。合法资源索引/PFN 不与负错误混淆。

`struct resource_handle { uint32_t index, generation; }` 两字段均须完整保存，不能只保留低 16 位。
READ=1、WRITE=2、MAP=4、RELEASE=8、TRANSFER=16、QUERY=32、ALL=63。
内核根据可信域 ID/epoch 和私有表检验授权，不依赖句柄保密。
错误码与生命周期沿用 [Phase 4](phase4-secure-bindings.md)：EPERM=-1、ENOENT=-2、ESRCH=-3、
EAGAIN=-11、ENOMEM=-12、EBUSY=-16、EINVAL=-22、ENOSPC=-28、EOVERFLOW=-75、ESTALE=-116。
加载器另使用 ENOEXEC=-8、EPROTONOSUPPORT=-93；它们是启动加载结果，不是新增 exec 系统调用。

## 外部程序启动约定

- 从 ELF entry 执行，CPL=3，CS=0x1b，SS/DS/ES/FS/GS=0x23。
- 默认 EFLAGS=2，IOPL/IF=0；抢占 profile EFLAGS=0x202，IF=1、IOPL=0。返回清理危险标志并保留算术标志/DF。
- 栈范围 `[0x083fc000,0x08400000)`，初始 ESP=0x083ffffc，满足 i386 C 入口对齐。
- 栈初始清零，顶部的零字只是防御性假返回地址；入口应 noreturn 并调用 exit。
- 没有 argc、argv、环境、auxv、TLS、libc 或 POSIX 启动语义。
- 通用寄存器初始为零。浮点/MMX/SIMD 未开放，CR0.TS 阻止扩展状态泄露。

动态资源窗口仍为 `[0x09000000,0x09010000)`，每域四个资源页、每资源四个别名，
全局 16 个资源槽及一条接收通知。外部映像页不是可释放资源句柄，不计入四页配额，
但加载本身受 64 个映像页及固定四页栈预算限制。

## Aelthuvor Phase 1 可以依赖什么

仅复制这三个公开头文件和加载 profile，不包含 kernel/、user/ 内部头或实现。
使用 freestanding i686 工具链、静态 ET_EXEC、公开 ELF note 和链接地址范围；
提供自己的 `_start`、入口栈使用约定、ABI 检查和低级资源封装，再用 initrd 运行。
LibOS 内存策略、数据结构和库实现由独立项目负责，不能假设有内核堆/文件/网络服务。
具体实现和验证见 Aelthuvor 仓库；内核只接收它的构建产物。

## 1.0 → 1.1 与独立 SDK 固定

1.1 仅增加 `SYS_RESOURCE_LIMITS=4` 和 LIMITS 位，原 0..3、16..23 与寄存器约定不变。
调用 4 不接受参数，只改 EAX/EDX。它返回配置上限（当前 4/4），不保证物理空闲页数量，
也不分配或授权任何资源。内核上限引用公开常量，避免两份数字漂移。
现有 1.0 ELF note 和客户端仍兼容；要求 1.1 的 Aelthuvor 会在旧内核上安全拒绝。

Aelthuvor 的 `sdk/manifest.json` 记录来源仓库、完整提交、ABI 版本与每文件 SHA256。
默认构建验证 vendored 文件，`make check-sdk SDK_SOURCE=../Liberums` 额外比较固定提交和当前
公开目录；集成测试也检查工作目录公开头一致。只升级内核而 SDK 不变时，不应隐式混入新头；
接口升级应单独审查、重新固定来源并运行两边测试。内核仓库不依赖 LibOS 源码或算法。

## 1.1 → 1.2：显式运行 profile 与撤销 ABI

保留 0..4、16..23 的号、参数、结果和权限常量。新增 24=POLL、25=INFO、26=ACK，
详见 [完整事件寄存器协议](phase6-revocation.md)。默认 profile 中三个新号返回 EPROTONOSUPPORT。
COOPERATIVE 的旧含义仍为“IF=0、只靠主动让出”，不在抢占 profile 报告。
ELF note 按选定 profile 的实际 flags 校验，要求 COOPERATIVE 的程序在抢占模式安全拒绝。
应用不能仅看 minor 推断执行模式，必须检查特性位；新接口仍属实验性，不承诺永久稳定。

Aelthuvor 当前固定 SDK 不应自动替换。本次没有改动或构建该仓库；旧 ELF 应继续选择默认 profile。
未来独立升级时，可以显式固定 1.2 的四个公共头、要求 PREEMPTION/REVOCATION、实现 POLL/INFO/ACK，
并更新该项目的精确版本测试与 SDK 来源检查。原 `tests/libos.py` 的 1.1 精确版本断言是历史验收工具，
未在本次冒充为已经运行的 Aelthuvor 1.2 集成；运行兼容性使用固定旧二进制另行验证。

## Phase 7.3 的外部设备使用

ABI 1.2 与原 ELF profile 不变。VGA/keyboard 是可探测的可选标量扩展，见公开 `vga.h`、`keyboard.h`。
ELF note 只声明 ABI/features，不构成设备 capability；必须由独立的可信启动配置授予资源。
普通 external 路径没有自动 grant。使用方式和权限边界见 [Phase 7.3](phase73-device-grants.md)。
