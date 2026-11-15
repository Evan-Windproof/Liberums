# Phase 4 — Resource Ownership & Secure Bindings

本阶段在 Phase 3 两个真实 Ring 3 域上加入物理页资源接口。原六页地址空间继续作为启动基础；
动态资源页与动态窗口页表单独追踪，不会把所有物理内存开放给用户。

用户现在可以申请一页、获得句柄、查询 PFN、选择允许的虚拟地址、映射、解除映射、释放，
也可以把一页独占资源安全转移给另一个存活域。普通访问由 MMU 检查，不再逐次陷入内核。
这些是受限的 Exokernel 保护实验，不等于整个 Exokernel 或 LibOS 已经完成。

## 一条完整路径

```text
Ring 3：page_claim / page_claim_specific
    -> int 0x80 / TSS 可信栈
    -> 域身份和 CR3 检查
    -> 配额、资源槽、物理页可用性检查
    -> 清零、写入所有权和代数 -> 返回 index + generation

Ring 3：page_map(handle, selected_va, rights)
    -> 所有者/域实例/代数/权限/状态检查
    -> 检查动态窗口及地址冲突
    -> 更新 PTE 和绑定记录、同步 TLB
    -> iret
Ring 3：普通 load/store -> MMU 直接执行权限检查
```

内核提供“这个页归谁、允许怎样访问”的保护机制；用户可以选择 PFN、VA 和管理顺序。
默认 claim 的线性扫描只是便利入口，claim_specific 让用户明确请求一个物理资源。
没有加入内核堆、文件系统、缓存或复杂分配策略。

## 容量与实验范围

- 全局 16 个资源槽；每域最多持有 4 页，包含尚未领取通知的转移页。
- 每资源最多 4 个别名绑定；每域动态窗口为 64 KiB，共 16 个虚拟页。
- 转移通知为每域一个收件槽，满时拒绝转移，不覆盖旧通知。
- 只有两个固定域，低 64 MiB 物理页范围、单 CPU、IF=0。

这些上限用于有界保护和便于审查，不代表资源公平调度。

## 与既有阶段的关系

Phase 1–3 测试和原用户映像保持不变。普通 `make run` 仍执行两个正常用户域的两轮运行，
不自动注入攻击。使用显式启动选项才运行 Phase 4 实验：

```sh
python3 tests/phase4.py --mode basic
python3 tests/phase4.py --mode transfer
make run QEMU_APPEND=resource-test=unmap-fault
make run QEMU_APPEND=resource-test=transfer-fault
make run QEMU_APPEND=resource-test=readonly
make test
```

basic/transfer 的完整敌对句柄实验由 GDB 把已知整数写入 B 的测试数据，
不改变授权；generation 实验也需要 GDB 对 FREE 槽注入边界值。请使用上述脚本，
不要把单独启动这些测试映像的完成字符串当作通过。

测试映像分为 BASIC、TRANSFER、FAULT 三组，各自仍不超过单页代码上限。
按启动参数选择映像，不改变基础六页布局，不引入通用 ELF 加载器。

推荐阅读：[所有权](phase4-resource-ownership.md) → [安全绑定及 ABI](phase4-secure-bindings.md)
→ [模块](phase4-modules.md) → [实际验收](phase4-validation.md)。

## 参考与未完成的方向

设计参照 MIT Exokernel 关于 Protection/Management 分离、Secure Bindings 与资源可见性的思想：
[1995](https://pdos.csail.mit.edu/6.1810/2017/readings/engler95exokernel.pdf)、
[1997](https://pdos.lcs.mit.edu/papers/exo-sosp97/exo-sosp97.html)。
本环境实际获取这两个原始 URL 时出站代理返回 403，因此不声称逐段核对或引用原文页码。
实现自行编写，没有复制 Xok/JOS 的资源管理代码；安全结论以本项目可执行测试及已说明的模型为界。

Phase 5 可研究显式撤销通知/确认协议与恶意域超时处理，但强制打断无限循环仍需要时钟抢占。
本阶段没有实现 LibOS、共享内存授权、SMP、DMA 授权或异步 Visible Revocation。
