# 14 Port移植层bm_port

> **本文职责**：说明量产 Port 文件要实现的驱动 API 符号与参考位置。  
> **不负责**：如何把库链进 Keil/IAR → [02-挂库到现有工程](02-挂库到现有工程.md)；HAL 契约语义 → [01-HAL契约与移植要点](01-HAL契约与移植要点.md)。

## 1. 定位

类比 FreeRTOS `portable/<compiler>/<mcu>/port.c`：Port 是**应用工程侧**的平台胶水，不在预编译静态库内。驱动 API 头文件为 `include/bm_drv_*.h`（与 `bm_hal_*.h` 同在 `include/` 根目录）。

**模板：** [`portable/template/bm_port.c`](../portable/template/bm_port.c)（组合模板：arch 提供 critical/memory，本文件仅 vendor 弱钩子）

**参考实现：** `portable/packs/*` 与 `portable/vendor/<mcu>/`（PC 开发可用 `BM_BACKEND=native_sim`）

## 2. 必须实现（或由 arch 层提供）

| 符号 | 用途 | 提供方 |
|------|------|--------|
| `bm_drv_critical_api` | 临界区进入/退出 | `portable/arch/<id>/`（`bm_arch_drv_bundle.c`） |
| `bm_drv_memory_api` | 内存屏障 | 同上 |

使用 `template/bm_port.c` 时：**不要**在应用 Port 中重复定义上述符号；链接对应 `bm_port_arch_<id>` 即可。

## 3. 按功能选用

| 符号 | 组件 |
|------|------|
| `bm_drv_timer_api` | 看门狗、HRT |
| `bm_drv_uart_api` | 日志、Shell |
| `bm_drv_pwm_api` 等 | 混合域外设 |

实现时对接厂商 SDK（Cube `HAL_*`、ESP-IDF `driver/*`）；当前量产参考为 `portable/vendor/esp32_idf/`（灯哥平衡车主控板 ESP32-WROOM-32E，**见 [08-ESP-IDF与灯哥平衡车集成](08-ESP-IDF与灯哥平衡车集成.md)**）与 `portable/sim/native/`。STM32/CH32/Nordic/NXP 等 vendor 暂缓恢复。

> **ESP32 特例：** `BM_BACKEND=sdk_esp32_idf` 时由 vendor 组件提供 `bm_drv_*`，**不必**复制 `bm_port.c`。

## 4. 集成检查清单

1. 从 `portable/template/bm_port.c` 复制到应用 `Core/Src/` 或 `source/`。
2. 按 PROFILE 实现本应用用到的 `bm_drv_*_api`。
3. 在 `native_sim` 或 QEMU 上跑通单元测试（若适用）。
4. 真机验证 HRT 时序与 ISR 上下文（混合域见 [03 §3.1](../01-应用开发/05-混合域接线.md#31-直接-hal-绑定不用-bm_exec)）。

## 5. 相关文档

| 主题 | 文档 |
|------|------|
| `portable/` 目录说明 | [01-HAL契约与移植要点](01-HAL契约与移植要点.md) |
| Cube / SDK 挂库步骤 | [02-挂库](02-挂库到现有工程.md)、[04-STM32](04-STM32-CubeMX集成.md)、[05-NXP](05-NXP-MCUXpresso集成.md) |
| ESP-IDF / 灯哥板 | [08-ESP-IDF与灯哥平衡车集成](08-ESP-IDF与灯哥平衡车集成.md) |
