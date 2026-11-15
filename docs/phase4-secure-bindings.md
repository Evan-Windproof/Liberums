# Secure Bindings、页表与资源 ABI

Secure Binding 是先完成授权检查，再建立可由硬件高效执行的访问绑定。
这里的绑定为“当前域实例拥有的资源页 → 当前域指定的用户虚拟页”，不是对所有内核资源的通用授权。

## 检查后再写 PTE

`resource_map()` 依次检查 index、OWNED 状态、完整 generation、owner_id/epoch、MAP 权限、
请求的 R/W 是否为资源权限子集、VA 对齐与范围、绑定槽容量，再调用分页模块。
分页模块拒绝已有 PTE，不做隐式覆盖。成功后才发布映射记录并增加 references。

动态窗口为 `[0x09000000, 0x09010000)`，不包含固定代码/数据/栈、探测别名或内核映射。
每个域拥有自己的动态页表，第一次映射时申请并清零；用户无法取得页表本身的用户访问权限。
PDE 和 PTE 均设置 U/S，数据写权限由 PTE.RW 控制。普通 i686 没有 NX，不宣称完整 W^X。

映射之后的 load/store 不经过系统调用。MMU 检查该 CR3 下的 PTE；只读写入得到 user #PF/error=7，
撤销后读取得到 user #PF/error=4。

## 撤销与 TLB

只清软件记录不能阻止访问。解除绑定先清对应 PTE，再对当前空间执行 invlpg。
若表中最后一个绑定被删除，清 PDE、重新加载当前 CR3 以移除页表缓存，再清零和释放页表页。

非活动空间不会执行：调度器先切到内核 CR3，未来重新进入前还要加载目标 CR3，
所以不保留可被旧域使用的非全局翻译。没有 SMP 或 global page，也没有设备 DMA 路径。

资源表记录每个别名。release、transfer、退出、故障清理都遍历整份绑定记录，不能只撤销最后一个 VA。
测试也保留第二个别名、只撤销第一个地址，然后访问旧地址，验证单页撤销时不依赖释放整张表。

## 系统调用 ABI

沿用 int 0x80；全部参数为整数，不接受用户指针。原 0/1/2 三个调用及其寄存器保持规则不变。
以下新接口中 EAX 是调用号/结果，EBX、ECX、EDX、ESI 为输入；除注明的 EDX 输出外其他通用寄存器保持。

| EAX 调用号 | 接口 | 输入 | 成功输出 |
| --- | --- | --- | --- |
| 16 | page_claim | EBX=rights | EAX=index（1..16），EDX=generation |
| 17 | page_claim_specific | EBX=PFN，ECX=rights | EAX=index，EDX=generation |
| 18 | page_map | EBX=index，ECX=generation，EDX=VA，ESI=R/W | EAX=0 |
| 19 | page_unmap | EBX=VA，仅当前域 | EAX=0 |
| 20 | page_release | EBX=index，ECX=generation | EAX=0 |
| 21 | page_query | EBX=index，ECX=generation | EAX=PFN，EDX=allowed rights |
| 22 | page_transfer | EBX=index，ECX=generation，EDX=目标 ID，ESI=新 rights | EAX=0 |
| 23 | page_receive | 无 | EAX=index，EDX=generation |

claim/specific/receive 失败时 EDX=0；query 失败时 EDX=0。
错误为 EAX 的负数补码，PFN 范围小于 16384，合法 index/查询结果不会与错误混淆。
用户封装的输出指针只用于把返回寄存器保存到自身内存，绝不作为内核系统调用参数传递。

## 权限位

READ=1、WRITE=2、MAP=4、RELEASE=8、TRANSFER=16、QUERY=32，ALL=63。
初始和转移的授权必须含 READ，不能出现未知位。map 只接受 READ 或 READ|WRITE。
MAP 表示建立绑定，QUERY 表示查看 PFN/rights；二者不是取得所有权的捷径。
解除自己的现有绑定不另设 UNMAP 位。内核生命期清理不受用户 RELEASE 位限制。

## 错误码

| 错误 | 值 | 代表情形 |
| --- | --- | --- |
| EPERM | -1 | 非所有者、域实例不符、权限不足、请求保留页 |
| ENOENT | -2 | 当前域无该绑定、收件槽为空 |
| ESRCH | -3 | 目标不存在或已结束 |
| EAGAIN | -11 | 目标收件槽已满 |
| ENOMEM | -12 | 没有物理页或无法申请动态页表 |
| EBUSY | -16 | 指定物理页已被占用、目标 VA 已有绑定 |
| EINVAL | -22 | 未对齐/越界 VA、非法 PFN/权限、自转移 |
| ENOSPC | -28 | 每域配额、资源槽或别名记录耗尽 |
| EOVERFLOW | -75 | 转移时无法前进代数 |
| ESTALE | -116 | 索引/代数无效、资源已释放 |

旧系统调用的 ENOSYS=-38 等行为不变。验证顺序决定一个同时违反多项规则的请求先返回哪个错误，
不保证把全部原因暴露给用户。所有错误路径都不得额外授予访问权限。
