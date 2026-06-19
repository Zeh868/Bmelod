# 08 ESP-IDF 与灯哥平衡车裸机集成

> 本文描述的是：使用 ESP-IDF 提供的 Xtensa 工具链与底层头文件，构建 Bmelod 的 ESP32-WROOM-32E 裸机后端。  
> 它不是普通的 ESP-IDF FreeRTOS 应用，不使用 `app_main` 任务模型，也不链接 FreeRTOS 运行时。

## 1. 目标与边界

| 项目 | 说明 |
|---|---|
| 目标硬件 | 灯哥 V4 平衡车主控板，ESP32-WROOM-32E |
| 构建方式 | 独立 CMake + ESP-IDF Xtensa 工具链 |
| 后端标识 | `BM_BACKEND=sdk_esp32_idf` |
| 裸机特征 | 无 FreeRTOS、无任务、无队列、无信号量、无事件组、无 `app_main` |
| 允许使用 | `soc/*_reg.h`、`hal/*_hal.h`、`hal/*_ll.h`、ROM API、Xtensa 工具链 |
| 不允许使用 | `freertos/*`、`esp_timer`、`esp_task_wdt`、任务式 `app_main`、隐式拉入 RTOS 的高级驱动 |

## 2. 当前板级能力

| 驱动 | 说明 |
|---|---|
| 定时器 | `bm_vendor_singleton_esp32_idf.c`，用于系统 tick / UART 钩子 / 看门狗钩子 |
| PWM | `bm_vendor_pwm_esp32_idf.c`，M0 / M1 三相输出 |
| ADC | `bm_vendor_adc_esp32_idf.c`，M0 / M1 电流采样缓存 |
| 编码器 | `bm_vendor_encoder_esp32_idf.c`，两路 AS5600 |
| BMI160 | `bm_vendor_bmi160_esp32_idf.c`，保留通用接口，板级连线待确认 |

`bm_hal_instances_esp32wroom32e.h` 维护板级 GPIO 宏；其中 `SDA1` 按模块脚 37 对应 `GPIO23` 处理，不使用 `GPIO37`。

## 3. 构建路径

### 3.1 独立 CMake

```cmake
cmake_minimum_required(VERSION 3.20)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

set(BMELOD_ROOT "/path/to/Bmelod")
set(BM_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BM_BACKEND "sdk_esp32_idf" CACHE STRING "" FORCE)

add_subdirectory("${BMELOD_ROOT}" bmelod EXCLUDE_FROM_ALL)
```

`portable/packs/sdk_esp32_idf/CMakeLists.txt` 会把：

- `portable/arch/xtensa`
- `portable/vendor/esp32_idf`

组合成 `bm_hal_esp32wroom32e`。

### 3.2 裸机启动与链接

裸机路径需要自行提供：

- 复位入口；
- `.data/.bss` 初始化；
- 栈顶与向量边界；
- `main()` 或等价的主循环入口。

仓库中的 `portable/boot/` 仅提供链接脚本与启动骨架参考，不代表普通 ESP-IDF 应用入口。

## 4. 不支持事项

- 不支持普通 `idf.py` + `app_main` FreeRTOS 应用；
- 不支持 Wi-Fi；`esp_wifi` 依赖 FreeRTOS / 事件循环 / 网络栈；
- 不支持把 `esp_timer`、任务 WDT、队列或信号量当成裸机基础设施；
- BMI160 的板级 I2C/SPI/INT 连接若未确认，不能猜测。

## 5. 验证建议

若本机存在 Xtensa 工具链与 ESP-IDF 头文件：

```bash
cmake -B build_esp32 -S . -DBM_BUILD_TESTS=OFF -DBM_BACKEND=sdk_esp32_idf -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-xtensa-esp32.cmake
```

随后检查：

- 源码、包含和链接清单中不出现 `freertos` / `xTask` / `queue` / `semaphore` / `event group`；
- 目标 ELF / map / nm 中不出现 `freertos`、`xTask*`、`vTask*` 等符号；
- `BM_BACKEND=native_sim` 仍可构建。
