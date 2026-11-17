# 学习路线

不要求一次读懂全部内核。按“最小示例 → 机制 → 源码 → 实际状态”循环。

| 层级 | 先做什么 | 读什么与练习 | 完成标准 |
|---|---|---|---|
| Beginner | make/run/debug，Hello LibOS | 架构 1/2/4、LibOS Lesson 1/2；改 LOG 消息 | 自己生成 ELF，在 CS=1b 正常 EXIT |
| Intermediate | 获得一页并切成对象 | 架构 3/5、Lesson 3/4；观察 handle/代数/PTE，改对象粒度 | 两种策略数据正确、旧句柄拒绝、资源回收 |
| Advanced | 多域、抢占、撤销、设备 | 架构 6/7/8、Lesson 5～10；主动 ACK、旧指针 #PF、PS/2 grant | 同机域隔离、真实 IRQ、失败不损害 peer |

基础准备：C 指针/结构体/位运算、栈与调用约定、二进制/十六进制。
不知道 CR3 时先用虚拟页翻译图，不必先看所有汇编异常入口。
对 first/best fit 的概念比较不要求读 Aelthuvor 完整实现。

[架构课程](architecture-guide/README.md) · [十课教程](libos-development/README.md) ·
[调试](debugging-guide.md) · [历史背景](history.md)。
