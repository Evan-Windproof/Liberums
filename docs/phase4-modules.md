# Phase 4 模块与调用关系

## `kernel/capability.c` / `include/kernel/capability.h`

纯整数的代数和权限检查，不访问物理内存或页表。
`resource_handle` 为 index/generation；`capability` 为 generation/owner_id/owner_epoch/rights。
`capability_next()` 在 UINT32_MAX 拒绝前进；`capability_rights_valid()` 检查初始授权；
`capability_check()` 返回成功、ESTALE 或 EPERM。
宿主 ASan/UBSan 直接测试同一份 C 实现的完整代数边界、域身份/实例和权限子集。

## `kernel/resource.c` / `include/kernel/resource.h`

资源保护的通用模块，维护 16 项静态资源表、每项四个绑定、状态和引用数。
接口为 claim、map、unmap、release、query、transfer、receive、cleanup，均显式传入可信调用域。
页号、权限及错误常量位于共享的 `resource_abi.h`，不把内核表暴露给用户。

- resolve 先检查资源状态，再使用 capability 模块验证授权。
- claim 检查配额/空槽/物理可用性，清零后发布所有权，失败不消耗槽或代数。
- map 先验证授权，后调用分页模块；PTE 创建失败不会发布引用记录。
- revoke_binding/revoke_all 处理全部别名，记录/PTE 矛盾严格 Panic。
- release/cleanup 清零并归还；transfer 先验证全部普通失败条件，再原子地撤销、清零、换身份。
- receive 领取单个通知；通知不取代所有权表的校验。

资源表在 kernel BSS，沿用 supervisor 映射。不是复杂内核堆，也不引入全局用户进程模型。
QEMU/GDB 测试核对所有 OWNED 行与实际 PTE 双向对应、引用数、唯一 PFN、配额和清理结果。

## 扩展 `arch/x86/paging.c` / `paging.h`

`address_space` 的 `pages[6]` 不变，新增独立 `dynamic_table` 字段。
`address_space_dynamic_address()` 验证窗口；`address_space_bind()` 在经过资源授权后建立 PTE；
`address_space_unbind()` 验证目标物理页、移除 PTE 并同步 TLB，空表立即释放。
低层接口只供可信内核调用，物理页所有权由上层 resource 模块检查。

`address_space_destroy()` 拒绝遗留动态表，防止把资源生命周期错误静默隐藏在六页释放中。
原基础布局、kernel identity map 和 CR0.PG/WP 不变。硬件故障、权限位和部分撤销路径由 GDB 验证。

## 扩展 `physical_pages.c` / `physical_pages.h`

只新增 `physical_page_alloc_specific(address)`：页对齐且 PAGE_FREE 才能切换为 PAGE_ALLOCATED。
它不判断用户身份、不新增用户分配策略，资源层在调用前检查请求 PFN 与权限。
原早期分配和状态追踪逻辑不改，原宿主/Phase 2 测试保持通过。

## 扩展 `domain.c` / `domain.h`

新增 epoch、resource_count 和 inbox。每次创建递增 epoch，不允许回绕。
`domain_lookup(id)` 仅返回 READY/RUNNING 域，因此不能把资源交给已退出域。
用户 exit/fault 返回可信调度栈并恢复内核 CR3 后，即执行 resource_cleanup，再允许同伴执行。
基础六页仍在本轮结束统一销毁；用户动态页不会被遗留到下一个域实例。

测试参数选择三个独立资源映像之一；正常启动仍使用原 test_a/test_b。
主流程 `main.c`、特权级切换汇编与异常来源检查无须改写。

## 扩展 `syscall.c` / `user/api.h`

沿用 `domain_from_trap` 的身份、CR3、可信栈检查。新增八种标量调用，读取保存的寄存器，
把 index/generation/rights/PFN 写回规定的返回寄存器。没有用户指针解引用。
旧 0/1/2 号行为不变，原寄存器/NT/DF 测试全部保留。

## 用户映像与测试

`user/resource_test.c` 定义基本、转移、故障/边界测试；编译为三个各不超过一页的映像。
条件编译仅用于测试分组，不隐藏内核保护控制流。`resource_entry.c` 选择 A/B 角色，
`resource_faults.S` 导出准确的非法读写指令位置，便于验证 EIP。
`user/linker.ld` 的 4096 字节断言和原六页模型不放宽。

`tests/phase4.py` 每种模式运行两个生命周期，使用 GDB 直接读取所有权、代数、PFN、CR3、PDE/PTE、
权限、异常帧、内容清零及最终分配状态。它在受控实验中把 A 的句柄整数写入 B 的私有测试数据，
用来证明“知道句柄仍无权限”；没有借此改动 PTE、owner 或授权。

`tests/resource_pressure.c` 只为显式 ENOMEM 实验暂存早期页，用户阶段后归还；
`resource_test.h` 声明选择测试模式和压力准备/结束接口。这不是生产资源授权机制。
`tests/capability_tests.c` 为宿主边界测试；Makefile 将 Phase 4 接入原 make test。

## 调用关系

```text
int 0x80 -> syscall_dispatch -> domain_from_trap
              -> resource_* -> capability_*（身份/代数/权限）
                            -> physical_page_*（取得/归还）
                            -> address_space_bind/unbind（PTE/TLB）

exit/fault -> 可信调度路径 -> resource_cleanup -> 原六页生命周期
```

各模块接口、限制与对应实验详见 [验收记录](phase4-validation.md)。
