# Lesson 4 — 内存策略为什么在用户态

一个 4096 字节页可以切成 64×64、32×128，或者实现线性 arena、first-fit、best-fit。
内核不知道这些对象；它只记录这页属于哪个域，以及哪些 VA 可访问。

[bitmap pool](../../examples/libos/memory-policy/pool.c) 用两个 uint32 的位表示最多 64 个对象。
first-free 搜索、边界检查、双重归还拒绝都在 Ring 3：

```c
uint32_t bit = 1u << (slot % 32);
if (!(pool->used[slot/32] & bit)) {
    pool->used[slot/32] |= bit;
    ++pool->live;
}
```

固定对象池简单、没有内部任意大小分块合并，但会有对象大小浪费。
线性 arena 的递增 offset 更简单，常只能整批重置；first/best fit 则能服务不同大小并产生不同碎片/复用选择。
这些都是可以替换的用户策略，不需要新 syscall。

```sh
make -C examples/libos/memory-policy SDK="$PWD/build/kernel-sdk" \
  OUT="$PWD/build/pool128" CFLAGS=-DOBJECT_BYTES=128
```

测试把新程序和 64 字节版本放入同一内核，逐字节验证两种布局、不同 PFN/CR3、配额与回收。
练习：实现一个自己的 linear arena 并保持同一 kernel SDK；对比空闲方式与 revoke 需要保存的 metadata。
本发布只提供经过实测的 bitmap 实现，不将练习的另一种算法冒充已实现功能。
