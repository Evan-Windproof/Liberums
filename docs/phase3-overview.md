# Phase 3 — User Domains & Privilege Isolation

Phase 3 在现有 Multiboot、异常和分页基础上，运行两个真实的 Ring 3 用户域。
它们有独立 CR3、私有代码/数据/用户栈，通过三个标量系统调用交还控制和输出诊断。
用户错误结束当前域；内核错误仍然 Panic。没有实现 LibOS、文件系统或 Phase 4 的授权系统。

开发基准为 `ed7f26344fbea0fa4b9f249a381fc37a0f2b4c17`，开始时工作区干净，
本地与远程 main 一致，原 Phase 2 验收先完整复跑通过。

## 整体流程

```text
原 _start / GDT / 栈
    -> 原串口、IDT、Multiboot、页状态、分页和 Phase 2 检查
    -> 原兼容性信息
    -> 扩展 GDT，加载 TSS
    -> 创建域 A 和 B（每域 6 个清零后的物理页）
    -> 设置当前域、TSS.ESP0 和 CR3
    -> iret -> CPL 3 用户程序
                 |
                 +-- int 0x80 -> TSS 内核栈 -> log/未知调用 -> iret 返回
                 +-- yield/exit -> 可信调度调用栈 -> 另一个 READY 域
                 +-- 用户异常 -> 记录 FAULTED -> 可信调度调用栈 -> 另一个域
    -> 切回内核 CR3，销毁两域的用户映射，清零并释放页
    -> 再创建运行一轮，验证重用
    -> Phase 3 完成，停止
```

正常启动输出两次 `ABab`：A 和 B 各输出大写字母，主动 yield 后，再各输出小写字母并 exit。
输出只是便于观察；自动验收还检查真实 CPL、页表、异常现场和页回收。

## 状态与边界

域的状态变化是 `READY -> RUNNING -> READY/EXITED/FAULTED`。
内核固定按 A、B 顺序选择 READY 域，没有优先级、线程、进程树或动态任务创建接口。
用户段基址为零，分页限制实际可访问地址。两个域在相同 USER_DATA 虚拟地址处，
看到的是不同物理页，不能借助低地址 identity alias 访问对方物理页。

当前采用合作式切换，IF=0。恶意程序可以无限循环，永不 yield；内核没有时钟抢占、
CPU 公平性或强制撤销保证。这项限制不影响已验证的内存与特权级访问边界。

## 可复现实验

```sh
make
make run                              # 两个正常用户域，每次启动重建两轮
make run QEMU_APPEND=domain-test=cross-read
make run QEMU_APPEND="domain-test=cross-write domain-reverse"
make run QEMU_APPEND=domain-test=cli
make run QEMU_APPEND=domain-test=allocation-failure
make debug
make test                             # 宿主测试与 Phase 1/2/3 的完整验收
```

攻击参数只选择内置测试映像的行为，不是用户可调用的页映射接口。`domain-reverse`
使 B 执行攻击、A 正常运行。所有故意故障 guest 都独立运行，普通启动不执行攻击。
最后一项先制造页耗尽，检查部分创建回滚，再继续正常用户实验。

阅读顺序：[模块](phase3-modules.md) → [特权级](phase3-privilege.md)
→ [地址空间](phase3-address-spaces.md) → [实际验收](phase3-validation.md)。
