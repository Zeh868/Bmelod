# bm_hal_memory — 内存屏障 HAL

头文件：`include/bm_hal_memory.h`

## 概述

为 `bm_bus` LATEST 等无锁结构提供 release / full 内存屏障，保证写操作对读者的可见性与读写有序性。须在链接阶段提供平台实现，不可使用未定义的弱符号默认值做生产用途。

## API

### `bm_memory_barrier_release()`

写屏障：确保调用点之前的写操作在后续读操作之前对其他核/ISR 可见。用于 `bm_bus` LATEST 写者侧更新发布索引之前。

### `bm_memory_barrier_full()`

全屏障：读写均有序。用于 `bm_bus` LATEST 读者侧标记读取索引之后、拷贝之前。

## 参考实现

| 平台 | 文件 | 实现 |
|------|------|------|
| native_sim | `portable/arch/host/` + `packs/native_sim` | `bm_atomic_ipc` 栅栏 |
| QEMU Cortex-M0 | `portable/arch/armv6m/` | 编译器屏障 |
| STM32G4 | `portable/arch/armv7em/` | `dmb` / `dsb` |
| CH32V003 | `portable/arch/riscv32/` | RISC-V `fence` |

## 移植要点

1. ARM Cortex-M：链接 `bm_port_arch_armv7em` 等 arch 包，或实现 `bm_drv_memory_api`。
2. 无缓存 MCU：release 可用编译器 `memory` clobber；DMA 一致性场景须用硬件屏障。
3. RISC-V：参考 `portable/arch/riscv32/` 中的 `fence` 实现。

## 与 bm_bus LATEST 的配合

```
写者: buffer[w] = data → barrier_release() → published = w
读者: reading = p → barrier_full() → 校验 published → copy → reading = NONE
```

缺少正确屏障可能导致读者读到撕裂数据或陈旧缓存。
