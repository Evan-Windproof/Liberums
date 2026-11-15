# Phase 5 — External LibOS Infrastructure

本阶段让 Liberums 能运行外部独立编译的 ELF32 程序，并公布实验性 ABI 1.0。
它没有实现 LibOS，也没有修改、连接或提交 Aelthuvor 仓库。
Aelthuvor 以后只需依照公开 ABI 编译自己的程序，作为 Multiboot 模块交给 Liberums。
内核不链接该项目的源码或库，也不按程序符号名识别某个 LibOS。

## 为什么将 LibOS 放在外部

内核负责隔离、资源授权和受控绑定；用户软件负责堆、缓存、线程等管理策略。
若把特定 LibOS 的实现编进内核，会使这些策略成为可信内核的一部分，也难以独立替换和实验。
当前只提供三个公开头文件 `include/abi/{syscall,resource,elf}.h`，均不依赖 kernel 私有结构。

## 两条启动路径

```text
原 Multiboot / IDT / 内存图 / 页状态 / 分页
    |
    +-- 默认：原 Phase 3/4 内置测试域，六页布局保持
    |
    +-- 显式 external：读取一个已保留的 Multiboot 模块
            -> 纯 ELF/ABI 校验，不分配页面
            -> 有界的独立页目录、用户页表、映像页、16 KiB 栈
            -> 使用经过验证的 entry / TSS / iret 进入 Ring 3
            -> 原 int 0x80 资源接口
            -> 正常退出或用户异常 -> 原资源清理 -> 映像/栈回收
```

外部 A 域与内置正常 B 域共同执行，继续采用固定顺序的合作式切换。
加载错误只拒绝 A，B 仍在真实 Ring 3 运行；不会把输入错误当作内核 Panic。
每次外部启动重建并运行两轮，用于检查数据清零和资源重用。

## 构建与运行

在 Liberums 根目录：

```sh
make
make external-fixtures
make run QEMU_APPEND=external QEMU_INITRD=build/external/normal.elf
make run QEMU_APPEND=external QEMU_INITRD=build/external/minimal.elf
make debug QEMU_APPEND=external QEMU_INITRD=build/external/normal.elf
make test
```

当前云环境先执行 `export PATH=/workspace/.liberums-tools/bin:$PATH`。
只有 `external` 这个完整 token 才选择外部加载；仅提供 initrd 不改变旧启动行为。
外部路径目前要求恰好一个模块，没有文件系统或运行时 exec 接口。
完成字符串表示控制流程结束；是否加载成功、退出正常，必须检查状态和自动测试。

## 独立构建示例

`tests/fixtures/external_user/` 有独立 Makefile，链接时只使用自己的对象文件：

```sh
make -C tests/fixtures/external_user \
  SDK=/absolute/path/to/public-sdk/include OUT=/tmp/external-demo
```

SDK 目录只需含 `abi/`。自动测试实际复制这三个公开头文件到隔离目录，再独立编译，
没有让测试程序包含内核私有头文件或链接内核实现。
该目录是加载器/ABI 测试工具，没有隐藏的堆、文件系统、POSIX 层或 LibOS。

最小映像验证版本、资源申请/映射/访问/归还/退出；多页映像包含三页代码、两页只读内容、
多页数据/BSS，并真正调用位于第三个代码页的函数。

## 阅读顺序和边界

[公开 ABI](phase5-abi.md) → [ELF 加载过程](phase5-elf-loader.md)
→ [模块](phase5-modules.md) → [实际验收](phase5-validation.md)。

仍无抢占、SMP、动态链接器、通用 ELF 特性、完整异步撤销或 LibOS。
普通非 PAE i686 没有 NX，不能把只读代码和 ELF 标志宣传为完整 W^X。
Phase 5 完成后停止；Aelthuvor 的实现属于另一个独立任务。
