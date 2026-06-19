# 09 灯哥 V4 电机 HAL 裸机实施计划

> 这份计划只针对灯哥 V4 的 ESP32 裸机后端：PWM / ADC / 编码器 / IMU。  
> 目标不是做一个普通 ESP-IDF 应用，而是保留 Bmelod 的确定性流式运行模型。

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

## 2. 当前实现状态

| 文件 | 状态 |
|---|---|
| `portable/vendor/esp32_idf/bm_hal_instances_esp32wroom32e.h` | 已补全板级宏与电气常量 |
| `portable/vendor/esp32_idf/bm_vendor_pwm_esp32_idf.c` | M0 / M1 PWM 实现中 |
| `portable/vendor/esp32_idf/bm_vendor_adc_esp32_idf.c` | M0 / M1 ADC 缓存采样中 |
| `portable/vendor/esp32_idf/bm_vendor_encoder_esp32_idf.c` | 两路 AS5600 读取中 |
| `portable/vendor/esp32_idf/bm_vendor_bmi160_esp32_idf.c` | 保留通用接口，板级连线未确认 |

## 3. 裸机约束

1. 不使用 FreeRTOS、任务、队列、信号量、事件组或 `app_main`。
2. 不使用 `esp_timer`、`esp_task_wdt` 或会隐式拉入 RTOS 的高级驱动。
3. 运行期对象全部静态分配。
4. ISR 里不能阻塞、不能打日志、不能动态分配。

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
