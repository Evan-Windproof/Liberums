# Phase 5 模块与接口

本页记录 Phase 5 的单 ELF/peer 路径，该兼容路径仍保留。
Phase 8 新增的多模块、四槽动态生命周期另见 [Phase 8 模块关系](phase8-modules.md)。

## `include/abi/`

独立 SDK，只有标准整数类型和公开结构/常量，不引用内核私有头。
resource.h 接收原资源常量和 handle 定义，syscall.h 定义旧调用与 ABI 查询/封装，
elf.h 定义装载 profile 和 note。旧私有头改为引用它们，测试模式枚举仍留在私有头。
兼容规则和返回寄存器详见 [ABI](phase5-abi.md)。

## `kernel/elf_loader.c` / `include/kernel/elf_loader.h`

纯、有界的 ELF 解析，不操作页表、不执行代码。`elf_validate(image,length,plan)`
返回成功、ENOEXEC 或 EPROTONOSUPPORT；`elf_plan` 保存 entry、最多八个段和总页数。
内部 read16/read32、validate_note、add_segment 分别负责字节读取、ABI 和段范围/权限校验。
没有用宏隐藏验证控制流。宿主 ASan/UBSan 与 QEMU 直接运行同一份解析代码。

## `arch/x86/user_space.c` 与扩展 `paging.h`

`address_space_create_external()` 只消费成功校验的 plan 和不可由用户改写的模块字节。
单个 allocate_page 辅助函数将每页立即加入回滚列表并清零。负责外部页表、文件复制、BSS 和栈，
不解析原始 ELF，不决定 LibOS 的高层布局策略。

`address_space.pages[]` 的容量扩大到 70，原 count=6 与其六个索引含义不变。
external 标记选择新激活条件；rollback_pages 仅记录失败时已分配页数，不授予权限。
原 destroy 统一处理有界页列表，拒绝活动 CR3 或遗留资源绑定。
`paging_copy_kernel()` 仅复制原 supervisor 内核 PDE，不复制其他用户域页表。
原 Secure Bindings 的 dynamic_table、权限检查和 TLB 操作保持。

## `kernel/external.c` / `include/kernel/external.h`

协调模块选择、纯校验、外部域/同伴域准备、执行和回收，不内联 ELF 解析。
当前只接受一个 Multiboot 模块。保存加载结果、失败前后空闲页数和 GDB 检查点，
输入失败返回报告而不是 Panic。每次启动运行两轮，同伴 B 始终使用原正常用户程序。
`external_prepared/reaped` 是无操作检查点，不包含伪造成功逻辑。

## `kernel/domain.c` 的小幅抽取

原域数组和调度行为保持，抽出 `domain_initialize()`（寄存器/身份）、`domain_schedule()`
（合作式执行及动态资源清理）、`domain_prepare_peer()` 和内部槽位访问接口。
外部构造使用验证后的 entry 和公开栈顶；旧路径继续设置原入口和原用户数据布局。
`arch_user_enter/leave`、TSS 栈和异常来源检查无需重写。

## `kernel/boot_info.c`、`main.c`、`syscall.c`

boot_context 保存已校验模块范围，沿用原保留区顺序，不改变 Phase 2 模块测试约定。
main 在原初始化后检查完整的 external token，调用新协调模块；默认继续 domains_run。
syscall 提供 ABI 查询号 3 和只读额度查询号 4，原 0/1/2 与 16..23 的含义和权限检查不变。

## `tests/fixtures/external_user/`

独立 Makefile，只使用 SDK 和自己的两个对象文件，不链接内核实现。
minimal.c 验证最小流程；program.c/padding.S 验证多页、BSS、quota、退出和故障。
链接脚本显式生成三个 LOAD 和一个 NOTE，不依赖运行时 section 表。
这是测试客户端，没有 LibOS、Aelthuvor、libc、堆或 POSIX 实现。

## `tests/elf_tests.c`、`tests/phase5.py`、`tests/elf_pressure.c`

宿主 runner 对同一个解析器执行有效/无效输入，并验证失败计划为空。
phase5.py 把 SDK 单独复制后编译，生成损坏 ELF，通过实际 QEMU/GDB 检查：
输入结果、CPL/CR3/TSS、全部用户映射、文件内容、BSS/栈清零、资源 API、故障和逐页回收。

压力辅助模块只在 external-oom 测试参数下暂存 RAM，留下五页，确保触发真实的部分构造失败。
恢复暂存页后同伴仍正常执行；生产加载器不会伪造分配器成功或跳过失败。

原测试脚本均不改断言；make test 继续原顺序，然后增加 Phase 5，另提供 external-fixtures 目标。
详见 [实际验收](phase5-validation.md)。

## 独立 LibOS 验收

`tests/libos.py` 由 `tests/phase5.py --aelthuvor /path/to/build` 调用。
只读取独立编译的 ELF、调试符号和映像字节，不导入 Aelthuvor C 源码或链接它的库。
GDB 为检查用户状态而装入外部符号不影响内核构建或授权。
`make test-libos` 通过独立仓库的 `make integration` 编排构建，再将 ELF 路径交给此脚本。
