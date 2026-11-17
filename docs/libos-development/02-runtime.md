# Lesson 2 — 你自己的 Runtime

[common/runtime.c](../../examples/libos/common/runtime.c) 是 Ring 3 源码，不是特权服务。
它选择一种自己的 `int libos_main(void)` 应用接口；你可以换成自己的 context 或对象模型。

```text
_start -> libos_start -> ABI_QUERY -> version/features check -> RESOURCE_LIMITS
       -> your application -> SYS_EXIT
```

```c
uint32_t features;
int32_t version = liberums_call(SYS_ABI_QUERY,0,0,0,0,&features);
if (version < 0 || !liberums_abi_compatible((uint32_t)version,features,1,2,required)) {
    liberums_exit(90);
}
```

先检测，再使用资源。对负数先检查再转 unsigned；不要把 -EPERM 当成巨大 handle。
示例保存信息到普通 ELF BSS，metadata 不放在之后可撤销的 payload。
正常返回统一变成 EXIT；错误码不假装成功。没有编译器浮点/SIMD 支持，不使用这些运算。

ABI Note 的 major/minor/features 与运行时检测必须一致。
`PROFILE=preempt` 需要 PREEMPTION；`PROFILE=coop` 要求 COOPERATIVE，不能塞进强制抢占的 multi-external。
实际构建 `make PROFILE=coop MIN_MINOR=1` 可生成旧 minor 合作程序；自动测试用原 external 路径运行两轮。

练习：改变最小 required feature，看 `-93` 的 ELF 拒绝与 runtime 主动错误是不同层次。
不能编辑旧 ELF Note 伪装兼容，也不需要 Aelthuvor 的 runtime 或 Application SDK。
