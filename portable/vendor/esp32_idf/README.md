# vendor/esp32_idf — 灯哥平衡车主控板 ESP32-WROOM-32E 裸机外设后端

本目录是 **灯哥平衡车** V4 主控板（ESP32-WROOM-32E）的裸机外设驱动层。
通过 `BM_BACKEND=sdk_esp32_idf` 由 `portable/packs/sdk_esp32_idf` 组合
`arch/xtensa` + 本目录一起引入，不作为普通 ESP-IDF FreeRTOS App 组件注册。

## 当前驱动能力

| 驱动文件 | 说明 |
|---|---|
| `bm_vendor_singleton_esp32_idf.c` | timer（TIMERG0 ISR tick）/ UART（ROM）/ WDG（TIMERG1 MWDT） |
| `bm_vendor_pwm_esp32_idf.c` | 三相 MCPWM 电机 PWM 输出（M0/M1，硬件 MCPWM unit0/1，ISR 驱动） |
| `bm_vendor_adc_esp32_idf.c` | 电机电流 ADC 采样（M0/M1 电流通道，ISR 内软触发） |
| `bm_vendor_encoder_esp32_idf.c` | 两路磁编码器（AS5600 I2C 读取） |
| `bm_vendor_bmi160_esp32_idf.c` | IMU（BMI160 接口，板级连线待确认） |

## 电机与引脚映射

电机驱动引脚（`bm_hal_instances_esp32wroom32e.h` 维护板级 GPIO 宏）：

| 信号 | 说明 |
|---|---|
| `M0_IN1/IN2/IN3` | M0 电机三相 PWM 输出 GPIO（via MCPWM0 GPIO matrix） |
| `M1_IN1/IN2/IN3` | M1 电机三相 PWM 输出 GPIO（via MCPWM1 GPIO matrix） |
| `M_EN` | 电机使能 GPIO（拉高使能，拉低安全态） |

> **待硬件确认**：电机 GPIO 编号与 AS5600 I2C/SDA/SCL 引脚在首次烧录上板时须与
> 实际 PCB 丝印核对；BMI160 的 I2C/SPI/INT 连接亦待确认后填入。

## 架构特征（Phase 2/3）

- **双核 SMP 隔离**：控制环（电流 ISR + PWM TEZ ISR）固定在 core1 高优先级中断，
  WiFi 可选在 core0（待硬件集成）。
- **硬件 MCPWM**：中心对齐 20 kHz，peak = 1000，由 MCPWM LL 直接配置。
- **WDT**：TIMERG1 MWDT 直接寄存器控制；IDF 5 Task WDT（`esp_task_wdt_config_t`）
  类型在 singleton 源文件中归档（待 FreeRTOS 路径切换时启用）。
- **无 FreeRTOS 运行时**：本路径不依赖 RTOS 调度、无任务/队列/信号量。

## 上层集成

```cmake
set(BM_BACKEND "sdk_esp32_idf" CACHE STRING "")
add_subdirectory(Bmelod)
target_link_libraries(my_app PRIVATE bm_hal_esp32wroom32e)
```

详见 `docs/03-移植与IDE集成/08-ESP-IDF与灯哥平衡车集成.md`。
