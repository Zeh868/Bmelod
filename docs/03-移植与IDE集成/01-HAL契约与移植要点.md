# 08 HAL契约与移植要点

> **本文职责**：HAL 契约、driver API 分层、参考平台与混合域外设移植要点。  
> **不负责**：Keil/IAR 加源文件 → [06-Keil](06-Keil-MDK集成.md)、[07-IAR](07-IAR-EWARM集成.md)；挂库总览 → [02-挂库](02-挂库到现有工程.md)。

应用与 `Source/` 只依赖 `include/bm_hal_*.h`；厂商 Port 在 `portable/`（量产用 `template/bm_port.c` 接 SDK）。混合域 **bind 如何接到向量 ISR** 见 [03 §3.1](../01-应用开发/05-混合域接线.md#31-直接-hal-绑定不用-bm_exec)。

## 三层结构（driver API + 架构 Port）

```text
include/hal/bm_hal_*.h      应用契约（稳定 API）
include/drv/bm_drv_*.h      驱动 API 表（Zephyr 同构 vtable）
include/port/               架构层契约（bm_arch_ops.h、bm_port_arch.h）
Source/hal/                 分发层（契约 → driver API）
portable/arch/              ISA 层（临界区 / 屏障，类比 FreeRTOS port.c）
portable/vendor/            芯片 SDK 外设层
portable/sim/               仿真后端（qemu_cm0、qemu_riscv64）
portable/packs/             BM_BACKEND 兼容包（组合 arch + vendor + sim）
portable/template/          量产 Port 模板
```

| 层级 | 职责 |
|------|------|
| 契约 `bm_hal_*` | 应用可见 API，不暴露 SDK 类型 |
| 驱动 API `bm_drv_*` | 子系统函数表 + 设备 `{api, config}` |
| 架构 `portable/arch/<id>/` | `bm_drv_critical_api`、`bm_drv_memory_api`（ISA 绑定） |
| vendor | Timer/UART/PWM 等对接 Cube / IDF |
| 分发层 | 未链接后端时返回 `BM_ERR_NOT_INIT` |
| `packs/` | 将 arch + vendor 链为 `bm_hal_<platform>`，保持 `BM_BACKEND` 不变 |

## 参考平台

| 后端目录 | 定位 |
|----------|------|
| `portable/sim/native` | PC 测试与示例（`packs/native_sim` = arch/host + sim/native） |
| `portable/vendor/esp32_idf` | 灯哥平衡车主控板 ESP32-WROOM-32E（`packs/sdk_esp32_idf` = arch/xtensa + vendor）；**集成步骤** → [08-ESP-IDF与灯哥平衡车集成](08-ESP-IDF与灯哥平衡车集成.md) |
| `portable/vendor/stm32g4` | **暂缓** — STM32G4 CMSIS 外设（后续恢复 `packs/sdk_stm32g4`） |
| `portable/vendor/ch32v003` | **暂缓** — CH32V003 外设（后续恢复 `packs/register_ch32v003`） |
| `portable/vendor/nordic_nrf52` | **暂缓** — nRF52 外设桩（链 `arch/armv7em`，无 pack） |
| `portable/vendor/nxp_kinetis` | **暂缓** — Kinetis 外设桩（链 `arch/armv7em`，无 pack） |
| `portable/packs/qemu_cortex_m0` | QEMU M0（arch/armv6m + sim/qemu_cm0） |
| `portable/packs/qemu_riscv64` | QEMU RV64 virt（arch/riscv64 + sim/qemu_riscv64） |
| `portable/boot/qemu_cortex_m0/` | QEMU M0 启动、`crt0`、链接脚本 |
| `portable/boot/riscv64/` | QEMU RV64 virt 启动、`crt0`、链接脚本 |
| `portable/template/bm_port.c` | 量产 Port 模板 |

## CMake 链接

```cmake
# 推荐：显式 arch + vendor（新）
set(BM_PORT_ARCH "xtensa" CACHE STRING "")
set(BM_PORT_VENDOR "esp32_idf" CACHE STRING "")

# 兼容：BM_BACKEND 解析为 packs/ 下的组合包
set(BM_BACKEND "native_sim" CACHE STRING "")
add_subdirectory(bmelod-baremetal)
target_link_libraries(my_app PRIVATE bm_framework bm_hal_native)         # PC
target_link_libraries(my_app PRIVATE bm_framework bm_hal_esp32wroom32e)  # 灯哥 ESP32-WROOM-32E
```

| `BM_BACKEND` | 解析 |
|--------------|------|
| `native_sim` | `packs/native_sim` |
| `sdk_esp32_idf` | `arch/xtensa` + `vendor/esp32_idf` |
| `arch_stub` | 仅 `arch/stub`，HAL 烟雾 |
| `sdk_stm32g4` / `register_ch32v003` | 暂缓，当前不随 `portable/vendor` 提供 |

详见 [03-Port移植层bm_port](03-Port移植层bm_port.md)。

**务必链接后端库**（`bm_hal_<platform>` 为 `bm_backend_*` 别名）。仅链 `bm_hal` 分发层时：

- `bm_hal_adc_bind_complete` 等返回 `BM_ERR_NOT_INIT`
- 无向量 ISR，绑定了也不会进电流环

见 [03-运行时约束与排障](../04-测试与排障/03-运行时约束与排障.md) fail-stop 说明。

## 混合域外设要点

| API | 行为 |
|-----|------|
| `bind_complete` / `bind_update` | 保存 `bm_hal_hrt_binding_t` 到 HAL 静态变量；ISR 调 `callback(context)` |
| `binding == NULL` | 先关中断源，再清 callback |
| 绑定 | **不得**隐式使能 PWM 输出或启动 ADC 序列 |

PWM/ADC/COMP/Encoder 契约头文件：`bm_hal_pwm.h`、`bm_hal_adc.h` 等。

移植检查：实现本应用用到的 API → `native_sim` 单测 → 真机时序。

## 移植（Port）与集成的关系

| 概念 | 内容 | 位置 |
|------|------|------|
| **库** | 事件、HAL 分发层、混合域 | `Source/`、`include/` |
| **Port** | `bm_drv_*_api` 实现 | [`portable/template/bm_port.c`](../portable/template/bm_port.c)，[03-Port移植层](03-Port移植层bm_port.md) |
| **集成** | 库怎么进 Keil/IAR/CMake | [02-挂库到现有工程](02-挂库到现有工程.md) |

本文描述 **Port 要实现什么**；挂库步骤见 [02-挂库](02-挂库到现有工程.md)。

## 相关集成文档

| 文档 | 内容 |
|------|------|
| [02-挂库到现有工程](02-挂库到现有工程.md) | 两步挂库总览 |
| [04-STM32-CubeMX集成](04-STM32-CubeMX集成.md) | Cube 工程 |
| [05-NXP-MCUXpresso集成](05-NXP-MCUXpresso集成.md) | MCUX 工程 |
| [06-Keil-MDK集成](06-Keil-MDK集成.md) / [07-IAR-EWARM集成](07-IAR-EWARM集成.md) | IDE 手工集成 |
| [08-ESP-IDF与灯哥平衡车集成](08-ESP-IDF与灯哥平衡车集成.md) | ESP-IDF + 灯哥板（无需 `bm_port.c`） |
