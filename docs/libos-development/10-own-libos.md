# Lesson 10 — 从空目录到你自己的 LibOS

这是可以实际执行的路线，完全不读 Aelthuvor：

```sh
# 在 Liberums 根目录：
make -j2 sdk
python3 scripts/new_libos.py --sdk build/kernel-sdk --output ../my-libos
# 新目录此前必须为空，生成器不会覆盖现有项目。
make -C ../my-libos
make run-libos LIBOS_ELF=../my-libos/build/libos.elf
```

项目中有自己的 main、support/start.S、support/runtime.c/.h、linker.ld、Makefile 与本地 sdk/。
从这里开始修改，而不是包含内核私有头或调用 Ael API。
例如将 main 改成：

```c
#include "runtime.h"
int libos_main(void) {
    int error = libos_log("My own runtime and application\n");
    return error ? 1 : 0;
}
```

只重建此目录，再加载新 ELF。用 `sha256sum build/liberums.elf` 确认内核没有重建。
再从空目录生成 `--example memory-policy` 项目，改变 OBJECT_BYTES、实现自己的对象管理；
`--example console` 则需可信设备 grant，不能因知道 SDK 就自动访问设备。

检查结果：ELF32/i386、原 ABI Note、无未定义符号、link map 不含宿主库/Ael、依赖文件只指向本项目和公共 SDK。
`make test-libos-examples` 自动完成空目录、修改消息/对象粒度、重建、真实 CPL3/CR3、数据和退出回收测试，
保存 Kernel/User ELF SHA256、编译命令、.d 与 link map。

最后可以自己重写 support/ 的入口与 runtime，定义不同于 libos_main 的应用 API。
它们是用户策略；保持公开 ABI 和错误/撤销契约即可，不要修改内核来迁就自己的 allocator。
GPL 派生示例保留 SPDX/许可证；发布自己的派生项目时记录来源与实际测试范围。
