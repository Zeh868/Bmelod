# 灯哥平衡车主控板 ESP32-WROOM-32E 后端（ESP-IDF 组件）

**详细集成步骤** → [docs/03-移植与IDE集成/08-ESP-IDF与灯哥平衡车集成.md](../../docs/03-移植与IDE集成/08-ESP-IDF与灯哥平衡车集成.md)。

基于 **ESP-IDF driver API**（`gptimer`、`uart`、`esp_task_wdt`）。临界区与内存屏障由 `arch/xtensa` 提供。

**模组：** ESP32-WROOM-32E  
**当前覆盖：** timer / UART / WDT（安全默认映射，不臆造板级 GPIO）  
**待补（需原理图/引脚表）：** 电机驱动、IMU、PWM、ADC 等外设

## ESP-IDF 工程集成（推荐）

1. 将 Bmelod 仓库加入 `EXTRA_COMPONENT_DIRS`，或把 `portable/vendor/esp32_idf` 复制为组件 `components/bmelod_esp32_backend`。
2. 在应用 `CMakeLists.txt` 中 `REQUIRES bmelod_esp32_backend`（或对应组件名）。
3. 链接 Bmelod 框架目标 `bm_framework` / `bmelod::framework`。

本目录含 `idf_component.yml`，要求 ESP-IDF ≥ 5.0。组件会一并编译 `arch/xtensa` 源文件。

## Bmelod 独立 CMake

```bash
export IDF_PATH=/path/to/esp-idf
cmake -B build -DBM_BACKEND=sdk_esp32_idf \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-xtensa-esp32.cmake
```

独立构建仅注入 ESP-IDF 头文件路径；**链接阶段仍需在 idf.py 工程中完成**。量产请使用上一节方式。

`BM_BACKEND=sdk_esp32_idf` 解析为 `arch/xtensa` + `vendor/esp32_idf`（`packs/sdk_esp32_idf`）。
