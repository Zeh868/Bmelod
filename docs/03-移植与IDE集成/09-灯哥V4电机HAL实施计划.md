# 09 灯哥 V4 电机 HAL 实施计划（IDF 组件 + SMP ISR 驱动）

> 本文描述灯哥 V4 ESP32-WROOM-32E 电机 HAL 的 Phase 2/3 实施现状，架构已迁移为
> **IDF 组件 + SMP 双核 + 控制环 core1 高优先级 ISR + 硬件 MCPWM**。
> WiFi 可选在 core0，但 Bmelod 控制环不依赖 WiFi 路径（待硬件集成）。

## 1. 目前确认的板级引脚

| 功能 | GPIO |
|---|---:|
| M0_IN1 | 32 |
| M0_IN2 | 33 |
| M0_IN3 | 25 |
| M1_IN1 | 26 |
| M1_IN2 | 27 |
| M1_IN3 | 14 |
| M_EN | 12 |
| M0 ia | 39 |
| M0 ib | 36 |
| M1 ia | 35 |
| M1 ib | 34 |
| VIN_MEA | 13 |
| 编码器0 SDA/SCL | 19 / 18 |
| 编码器1 SDA/SCL | 23 / 5 |

其中编码器 1 的 `SDA1` 按模块脚 37 对应 `GPIO23` 处理。

## 2. 当前实现状态（Phase 2/3 已完成）

| 文件 | 状态 |
|---|---|
| `portable/vendor/esp32_idf/bm_hal_instances_esp32wroom32e.h` | 已补全板级宏与电气常量 |
| `portable/vendor/esp32_idf/bm_vendor_singleton_esp32_idf.c` | timer ISR + UART + MWDT WDG 已完成 |
| `portable/vendor/esp32_idf/bm_vendor_pwm_esp32_idf.c` | 硬件 MCPWM ISR 驱动已完成（**待硬件出波验证**） |
| `portable/vendor/esp32_idf/bm_vendor_adc_esp32_idf.c` | MCPWM TEZ ISR 内软触发采样已完成（**待硬件验证**） |
| `portable/vendor/esp32_idf/bm_vendor_encoder_esp32_idf.c` | AS5600 I2C 接口已完成（**待硬件验证**） |
| `portable/vendor/esp32_idf/bm_vendor_bmi160_esp32_idf.c` | 保留通用接口，板级连线**待硬件确认** |
| `portable/vendor/esp32_idf/idf_component.yml` | IDF 组件清单（依赖 idf>=5.0） |

## 3. 驱动层约束

1. 控制环 ISR（MCPWM TEZ + Timer tick）固定在 core1 高优先级中断，WiFi 在 core0（待集成）。
2. 不在 ISR 内动态分配（无 `malloc`/`pvPortMalloc`/`heap_caps_malloc`）。
3. 运行期对象全部静态分配；所有 API 表为 `const` 全局对象。
4. `esp_task_wdt_config_t` 类型在 singleton 中归档，待 FreeRTOS 应用路径时启用；
   当前裸机路径使用 TIMERG1 MWDT 直接寄存器控制。
5. `driver/` 高级封装层、`esp_wifi`、`malloc/free` 在裸机 vendor 层禁用（failfast 扫描）。

## 4. 硬件确认要求

BMI160 的 I2C / SPI / INT 拓扑必须先由原理图或实物确认，再做板级绑定。  
仓库当前文本可抽取原理图没有直接确认这部分连线，所以现在只保留驱动接口，不猜 GPIO。

## 5. 验收建议

可用的验证项：

- `rg` 搜索裸机后端源码、包含和链接清单，不出现 `freertos` / `xTask` / `queue` / `semaphore` / `event group`；
- 若有工具链，构建 ESP32 ELF，并用 `nm` / `map` 检查无 RTOS 符号；
- `BM_BACKEND=native_sim` 继续可构建；
- PWM 安全态：`M_EN` 拉低、占空比清零；
- 编码器：两路 AS5600 各自静态绑定到独立 I2C 资源；
- BMI160：未确认拓扑前不做板级猜测。

## 6. 备注

如果后续确认 BMI160 具体连线，再补：

- 板级 GPIO 宏；
- I2C / SPI 资源分配；
- INT 中断接管；
- 原始采样 API 与错误路径。
