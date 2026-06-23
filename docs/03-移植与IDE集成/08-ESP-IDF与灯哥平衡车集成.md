# 08 ESP-IDF 与灯哥平衡车集成

> 本文描述：将 Bmelod 裸机平衡车控制库以 **IDF 组件**方式集成到 ESP32-WROOM-32E 平台，
> 使用硬件 MCPWM、控制环 ISR 跑在 core1 高优先级中断，WiFi 可选在 core0 并行运行。

## 1. 目标与边界

| 项目 | 说明 |
|---|---|
| 目标硬件 | 灯哥 V4 平衡车主控板，ESP32-WROOM-32E |
| 构建方式 | ESP-IDF 组件（`idf_component.yml`）+ `idf.py build` |
| 后端标识 | `BM_BACKEND=sdk_esp32_idf` |
| 架构特征 | IDF 组件 + SMP 双核 + 控制环 core1 高优先级 ISR + 硬件 MCPWM |
| 允许使用 | `soc/*_reg.h`、`hal/*_ll.h`、`esp_intr_alloc`、ROM API、Xtensa 工具链 |
| 不允许使用 | `driver/`（高级驱动层封装），`pvPortMalloc`/`heap_caps_malloc`（ISR 内动态分配） |
| WiFi/BT | 可在 core0 并行运行；WiFi/BT 联网为**待硬件集成项** |

## 2. 架构（IDF 组件 + SMP + 控制环 core1）

```
ESP32-WROOM-32E（双核 LX6，240 MHz）
├── core0  IDF 调度器、WiFi 驱动（待硬件集成）、app_main 初始化
└── core1  控制环 ISR（高优先级）
            ├── MCPWM0/1 TEZ ISR → ADC 软触发采样 → 电流环回调
            └── TIMERG0 timer0 ISR → 系统 tick → 框架 tick 回调
```

## 3. 当前板级能力

| 驱动 | 说明 |
|---|---|
| 系统 tick | TIMERG0 timer0 + ISR，`bm_vendor_singleton_esp32_idf.c` |
| PWM | MCPWM unit0/1，三相中心对齐 20 kHz，`bm_vendor_pwm_esp32_idf.c` |
| ADC | M0/M1 电流通道，MCPWM ISR 内软触发，`bm_vendor_adc_esp32_idf.c` |
| 编码器 | AS5600 I2C，`bm_vendor_encoder_esp32_idf.c` |
| IMU | BMI160（板级连线**待硬件确认**），`bm_vendor_bmi160_esp32_idf.c` |
| WDG | TIMERG1 MWDT（直接 LL 寄存器），`bm_vendor_singleton_esp32_idf.c` |

`bm_hal_instances_esp32wroom32e.h` 维护板级 GPIO 宏；其中 SDA1 按模块脚 37 对应
`GPIO23` 处理，不使用 `GPIO37`。

## 4. IDF 组件清单（idf_component.yml）

`portable/vendor/esp32_idf/idf_component.yml` 已提供最小清单：

```yaml
## 灯哥平衡车主控板 ESP32-WROOM-32E 后端（ESP-IDF 组件）
dependencies:
  idf: ">=5.0"
```

## 5. 构建路径

### 5.1 IDF 组件方式（推荐）

在上层 IDF 应用工程的 `CMakeLists.txt` 中引入 Bmelod：

```cmake
cmake_minimum_required(VERSION 3.20)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(BMELOD_ROOT "$ENV{BMELOD_ROOT}")
set(BM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BM_BACKEND "sdk_esp32_idf" CACHE STRING "" FORCE)

project(my_balance_car)
add_subdirectory("${BMELOD_ROOT}" bmelod EXCLUDE_FROM_ALL)
target_link_libraries(my_balance_car.elf PRIVATE bm_hal_esp32wroom32e)
```

`portable/packs/sdk_esp32_idf/CMakeLists.txt` 会把：

- `portable/arch/xtensa`（Xtensa 临界区 / 屏障）
- `portable/vendor/esp32_idf`（MCPWM / ADC / Timer / WDG 外设驱动）

组合为 `bm_hal_esp32wroom32e`。

### 5.2 启动与链接

IDF 组件路线由 `idf.py` 构建系统提供完整的启动、链接脚本与复位入口，
**不**使用仓库内的 `portable/boot/`（该目录仅包含 QEMU M0 / QEMU RV64 的裸机骨架）。

## 6. 不支持事项（当前阶段）

- 不支持把 `esp_timer`、任务 WDT（`esp_task_wdt_init`）当作裸机基础设施在 ISR 中调用；
  `esp_task_wdt_config_t` 类型已在 singleton 中归档，待迁移 FreeRTOS 路径后启用。
- BMI160 的板级 I2C/SPI/INT 连接若未确认，不能猜测。
- WiFi/BT 联网为**待硬件集成项**：`esp_wifi` 依赖 FreeRTOS 事件循环，可在 core0 运行，
  但 Bmelod 控制环不依赖 WiFi 路径。

## 7. 验证建议

独立编译冒烟（无需上板）：

```bash
cd portable/vendor/esp32_idf/_compilecheck
cmake -B build -DCMAKE_TOOLCHAIN_FILE=toolchain-zephyr-xtensa-esp32.cmake
cmake --build build
```

待硬件：

- 烧录后确认 MCPWM 出波（示波器或逻辑分析仪）；
- 确认 AS5600 I2C 通信；
- 确认 BMI160 连线后启用 IMU 驱动；
- WiFi 联网调试在 core0 初始化后进行。
