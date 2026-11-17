# v1.0.0 教学发布：验证与交付

本页是最终教学基线的验收索引，不替代历史报告。
开发基准：`2a66612b2823fe7b010257373b23f9aa38252eb0`；开发前 main/HEAD 一致、工作区干净。
版本由维护者确认采用 **v1.0.0**，稳定含义仅限教学范围、ABI 与可复现验证，不是生产认证。
内核/arch/公开及私有头/旧 user 源码与 Phase 9 保持，新增工作在 SDK、Ring 3 示例、工具、测试和文档。

## 实际复现命令

```sh
make clean
make -j2
make test
make test-phase8
make test-phase9
make test-libos-examples
make test-phase9-e2e PHASE9_AEL_BUILD=/path/to/P4/build
make test-libos-compat PHASE10_AEL_BUILD=/path/to/P4/build
python3 scripts/check_docs.py
```

完整编排可用 `make validate-release VALIDATION_ARGS='--aelthuvor-build /path/to/P4/build --historical-checkouts /path/to/P3 /path/to/P2 /path/to/ABI11'`。
它执行真实命令并记录 exit code，不以日志含 PASS 代替返回值。
整个编排冻结内核 ELF SHA；外部消费者改变/重建用户代码时不得重建内核。

## 验收矩阵与统计

| 范围 | 结果／计数来源 |
|---|---|
| 开发前基准 | 原 Phase 1～9 完整 make test 退出 0 |
| 干净构建/历史回归 | make clean、make -j2、make test，以及独立 test-phase8/9 均执行，原断言保留 |
| 中立教学套件 | 11 个真实 QEMU/GDB guests：minimal、bitmap、仓库外编辑、两粒度、fault、ACK/safe/#PF、ABI1.1合作两轮、设备拒绝/Console |
| 新多 LibOS | 4 个 Aelthuvor First/Best + bitmap 正常/故障实验，各自 CR3/策略/资源，均执行完整回收 |
| 原 Phase 8 | 19 QEMU guests（18 GDB + 1 make run-multi），24 轮复用等原覆盖保持 |
| 原 Phase 9 core | 5 个 QEMU/GDB guests，包含单次启动 128 轮 EXIT/#UD 复用、Epoch 1→129 |
| 原外部 RAM E2E | 21 个 QEMU/GDB guests，含 core 五项和 first/best 各八项；不再把 core 重复叠加为 26 |
| 外部 Ael Phase 4 | 自有 SDK/Reference 集成 13 guests/18 lifecycles；memory 13/26；Console 9（11 完成 + 1 持续会话） |
| 历史兼容 | Phase 3 memory 13/26、Console 9（11 完成 + 1 持续）；Phase 2 13/26；历史 ABI1.1 suite 15/30 |
| 宿主/工程检查 | 原内存/Capability/撤销/设备/域池 native，另有 SDK byte/hash、缺工具/SDK、无 Git 源码导出、配置切换及拒绝覆盖项目检查 |

数字从实际 runner JSON/完成记录导出，不能把重复执行加成“独立覆盖率”。
宿主模拟、编译、真实 guest 运行、正确终止、资源回收分开；持续 Console 由宿主停止，不计作完成 lifetime。

## 新消费者证据

测试先创建空的仓库外目录，只复制公共 SDK 和教学 support/应用文件。
修改 Hello 消息及 OBJECT_BYTES=128，再编译并真正执行对应新 ELF。
记录 Kernel/User ELF SHA256、实际编译命令、每次 .d/link map、来源文件哈希和前后内核 SHA/mtime。
依赖文件必须全部落在导出的独立项目内；无 Ael/内核私有头、静态库、宿主 libc 或其他 checkout 隐式读取。

GDB 检查 file/BSS、ELF RX/R/RW、私有 CR3/用户栈、CPL3、TSS/内核栈、owner ID/Epoch、Generation、
PTE/权限、页内容/资源配额、IRQ0 与最终清零/回收。新普通 RAM ACK 路径保留实际 POLL/INFO/ACK trace；
强制路径由真实 tick 到期并检查 PAGE_FREE/1024 零字及旧指针 #PF，不能用改 PTE 冒充。

Console 使用 QMP input-send-event→PS/2→IRQ1→用户扫描码解释→VGA；检查真实字节、EOI、MMIO PTE、文本与释放。
截图是 QEMU renderer 结果，不是 GTK 窗口。云端 display backend 只有 none；
本阶段没有人工验证 WSLg 窗口或实体键盘，原本地运行入口保留。

## 发布产物、来源与状态

固定 commit 的 source、SDK、examples tar.gz，Release Notes、validation.json、manifest.json、SHA256SUMS
及可选的受测 kernel ELF 放在 `build/release/`，不提交工具链、VM 镜像、缓存或生成对象。
源码/SDK/示例归档对固定 commit 可重复生成；内核 ELF 记录工具链、条件与实测 SHA，
不声称跨工具链/调试路径/编译时间完全 bit-identical。

最终源码 commit、Tag 推送结果及本机可下载产物由交付报告给出。
**当前环境访问 GitHub GraphQL/REST 均返回 403，无法创建或核实 GitHub Release 对象。**
正常 Git 提交/推送与新 Tag 可以单独完成；准备的产物和 Tag 不能冒充“Release 已发布”。
拥有 API 权限的维护者可按 [Maintainer Guide](maintainer-guide.md) 补发布，不覆盖旧 tag/release。

证据目录：[release-evidence](release-evidence/README.md)；构建输出 build 保持 Git 忽略。
原 Phase 1～9 报告与失败记录不移动、不删除；只通过 [history](history.md) 解释时效。
