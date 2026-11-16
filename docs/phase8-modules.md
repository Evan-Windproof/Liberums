# Phase 8：模块关系与维护入口

```text
boot_info_read → external_config_read → multi_domains_run
                                      └→ domain_create_external
                                          ├→ elf_validate_features（原 parser）
                                          ├→ domain_reserve
                                          ├→ address_space_create_external（原分页）
                                          ├→ domain_initialize（原现场）
                                          └→ device_grants_apply（原授权）
multi_domains_run → domain_schedule → domain_run_slice → arch_user_enter
                       ↑                  ↓ EXIT/YIELD/#PF/IRQ0
                       └──── arch_user_leave
                       └→ domain_cleanup（设备 → 普通 RAM / revoke / inbox）
multi_domains_run → domain_reap → cleanup → space destroy → context/stack zero → UNUSED
```

- `include/kernel/domain.h`：唯一域数 `DOMAIN_CAPACITY`，旧状态数值保持；结构追加私有生命周期字段。
- `kernel/domain_pool.c` / `include/kernel/domain_pool.h`：可信生命周期接口及脏槽/活跃槽保护。
  `pool_last` 和三个 NOP checkpoint 只供调试，绝不参与授权或成功判定。
- `kernel/multi_external.c`：模块序号到实际域 ID/Epoch/result 的记录；逐项加载、报告、运行与回收。
- `kernel/external_launch.c`：multi profile 和显式模块授权的解析；原单 ELF 配置保持。
- `kernel/scheduler.c`：原有界轮转改用容量常量，终止清理共用 helper；不含应用分支。
- `kernel/{resource,revoke,keyboard,vga_device}.c`：只读 busy 检查供域池确认无残留授权。
- `tests/domain_pool_tests.c`：真实域池代码，ELF/RAM/硬件端点模拟，ASan/UBSan 边界测试。
- `tests/fixtures/domains8/`：只包含公开 ABI 的独立用户对抗程序和非合作寄存器循环；两个独立编译 ELF。
- `tests/phase8_runtime.c`：只有显式 `selftest=phase8` 才启用的可信故障/复用/撤销驱动。
- `tests/phase8.py`：QEMU/GDB 四域、资源、设备和复用；只向私有测试输入和诊断请求写数据。
- `tests/phase8_ael.py`：可选的独立 Aelthuvor ELF 并存验收，不构建、不改动该仓库。

旧 Phase 5 模块文档描述的单模块、双轮路径仍保留；新的多模块机制在本页单独说明。
新增硬件中断观察符号不改变汇编执行序列。COM1、原 run 系列入口和全部旧回归继续存在。
