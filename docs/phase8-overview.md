# Phase 8：动态用户域与资源管理

本阶段在 Phase 7.3 `5d9c8f35a4ae54ef08b93b7f4d5fecd9e350acbf` 上扩展。
开发前核对了工作分支、HEAD、远程 main 和干净工作区，阅读了 Phase 1～7.3 的代码与文档，
完成旧回归基准后才接入新路径。没有重新实现启动、ELF 校验、分页、Capability、设备或用户 Console。

## 审查结果与迁移方案

原实现有两个静态 domain 对象，每域有 8 KiB 内核栈、独立页目录和用户栈。
外部程序占槽 1，内置 peer 占槽 2；外部默认运行两轮。调度器遍历固定 ID 1/2，
终止后回收设备与资源页，外部启动者另行销毁地址空间。资源表共 16 项，每域最多 4 项，
每资源最多 4 个绑定；这些配额与域数量含义不同。

迁移只扩展域对象数组、槽位边界和调度遍历，增加可信域池及多模块启动者。
旧 profile 保留原启动、终止观察和两轮行为；新 profile 使用显式 reap 发布空闲槽位。
撤销事件由 4 项扩为 8 项，每域上限仍为 2。公开 ABI 与配额均不变。
VGA/Keyboard 沿用原独占实现，增加只读生命周期检查函数，未建立通用驱动框架。

## 容量评估

| 项目 | 当前界限及含义 |
|---|---|
| 并存用户域 | 4；对象静态有界，生命周期动态分配 |
| 内核栈 | 每域 8192 字节，四域共 32 KiB；比原来增加 16 KiB |
| 外部地址空间 | 每域最多 70 个 RAM 页，四域上限约 1.1 MiB，含目录/映像/用户栈 |
| 普通资源 | 全局 16 页，每域 4 页，每资源 4 绑定；额外映射表独立计入物理页 |
| 撤销事件 | 全局 8 项，每域 2 项 |
| Multiboot 模块 | 最多 8 个，逐个验证；同时成功建立最多 4 个域 |
| 用户栈 | 每域独立四页，VA `0x083fc000..0x08400000` |

在 32 MiB、单 CPU QEMU 中有足够余量，且真正分配仍检查 OOM 并回滚。
不是随机器内存自动增长的域表，也没有用户态 spawn 服务。
本次最终 ELF 的 GDB 类型布局为 `sizeof(struct domain)=8640`、`sizeof(domains)=34560` 字节，
`size` 记录 BSS 167956 字节；栈预算包含在域对象数组中，不能重复计算。

## 运行方式

```sh
make -j2
make run                       # 原默认无窗口行为
make run-multi                 # 两个不同的独立 ABI 测试 ELF，完成后内核停机等待
make run-multi MULTI_ELFS="build/phase8/worker-a.elf,build/phase8/worker-b.elf,build/phase8/worker-a.elf"
make test-phase8
make test                      # Phase 1～8；包括原设备/串口测试
```

QEMU `-initrd` 的逗号列表产生多个真正的 Multiboot 模块，不是用户程序拼接容器。
模块 payload、描述符、名称串仍由原 boot parser 保留，不能被分配器覆盖。
`multi-external` 强制选择已有抢占 ABI profile，因此要求 ELF note 的 feature 集兼容抢占。
历史 cooperative ELF 仍使用原 `external` 路径，不能声称 cooperative 程序已具备强制 CPU 隔离。

设备只通过可信启动参数显式授权，例如模块序号 1 的
`multi-vga=1 multi-keyboard=1`；序号为 1～8，是本次 Multiboot 模块列表中的位置。
不是分配后的 Domain ID，坏模块不会改变后续授权对象。只读 VGA 可用 `multi-vga-readonly=1`。
ELF note、文件名、入口地址和 Aelthuvor 身份均不授予权限。冲突授权只使该次创建失败。

图形实验可直接使用原 `make run-external-console CONSOLE_ELF=... QEMU_DISPLAY=gtk`。
多域 Console 的完整 QEMU 示例与本地限制见 [验收](phase8-validation.md)。
[生命周期](phase8-domain-lifecycle.md)、[调度](phase8-scheduler.md)、
[资源](phase8-resource-management.md)、[模块](phase8-modules.md)、
[边界审查](phase8-boundary-review.md) 分别说明保护机制。
