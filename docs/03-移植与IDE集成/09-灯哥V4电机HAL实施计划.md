# 09 灯哥 V4 电机 HAL 实施计划

> **本文职责**：在 `08-ESP-IDF与灯哥平衡车集成.md` 的基础上，完成灯哥 Dengfoc-V4 双电机 FOC 板的电机驱动层（PWM / ADC / 编码器）实现规格与执行步骤。  
> **目标硬件**：嘉立创 Dengfoc-V4，主控 ESP32-WROOM-32-N8，双电机（M0 / M1），FD6287T 栅极驱动，INA240 电流采样，AS5600 磁编码器 ×2。  
> **对应代码路径**：`portable/vendor/esp32_idf/`

---

## 1. 板级引脚映射（Dengfoc-V4，已从原理图提取并确认）

### 1.1 PWM 输出（→ FD6287T 栅极驱动）

FD6287T 每通道接受一路 PWM 输入，低边互补与死区由驱动内部生成，MCU 侧只需三路 PWM。

| 信号 | 网络名 | ESP32 GPIO |
|------|--------|------------|
| M0 相A | M0_IN1 | **32** |
| M0 相B | M0_IN2 | **33** |
| M0 相C | M0_IN3 | **25** |
| M1 相A | M1_IN1 | **26** |
| M1 相B | M1_IN2 | **27** |
| M1 相C | M1_IN3 | **14** |

### 1.2 栅极使能

| 信号 | 网络名 | GPIO | 极性 | 器件 |
|------|--------|------|------|------|
| 电机总使能 | M_EN | **12** | **高有效** | SS8050 NPN，上电默认低=禁止 |

### 1.3 电流采样（INA240 → ADC1）

ADC1 不与 WiFi 冲突，双电阻采样（rank0=ia，rank1=ib）。

| 信号 | 网络名 | GPIO | ADC1 通道 |
|------|--------|------|-----------|
| M0 ia | M0_CS1 | **39** | CH3 |
| M0 ib | M0_CS2 | **36** | CH0 |
| M1 ia | M1_CS1 | **35** | CH7 |
| M1 ib | M1_CS2 | **34** | CH6 |

### 1.4 母线电压

| 信号 | 网络名 | GPIO | ADC 通道 | 分压比 |
|------|--------|------|----------|--------|
| 母线分压 | VIN_MEA | **13** | ADC2_CH4 | 1/8.5（R12=7.5kΩ / R13=1kΩ）|

> ⚠️ **ADC2 与 WiFi 互斥**：GPIO13 使用 ADC2，开启 WiFi 时无法读取。量产使用 BLE/STA 需评估此约束或切至外部 SPI-ADC。

### 1.5 磁编码器（AS5600 × 2，I2C 地址均为 0x36）

两颗芯片地址相同，**必须各占独立 I2C 总线**。

| 编码器 | 信号 | GPIO |
|--------|------|------|
| 编码器0（M0） | SDA0 | **19** |
| 编码器0（M0） | SCL0 | **18** |
| 编码器0（M0） | CS0  | **22**（当前仅作预留引脚宏，AS5600 I2C 模式下 CS 接 GND） |
| 编码器1（M1） | SDA1 | **23** |
| 编码器1（M1） | SCL1 | **5**  |
| 编码器1（M1） | CS1  | **21**（同上预留） |

### 1.6 其它（引脚宏预留）

| 功能 | GPIO | 说明 |
|------|------|------|
| RGB WS2812B 数据 | **2** | 本批仅保留宏，驱动可后续扩展 |
| 扩展 UART1 TXD | **17** | CN1，可选通信 |
| 扩展 UART1 RXD | **16** | CN1 |
| 扩展 IO | **15 / 4** | CN2 |

---

## 2. 待实现文件清单

### 2.1 引脚映射头（修改）

```
portable/vendor/esp32_idf/bm_hal_instances_esp32wroom32e.h
```

补全所有引脚宏（PWM × 6、M_EN、ADC × 4、VIN_MEA、I2C × 4、RGB），Doxygen 中文注释。

### 2.2 电机 HAL 源文件（新增）

建议拆为三个文件，也可合为一个，保持与现有 singleton 文件风格一致：

| 文件 | 实现内容 |
|------|----------|
| `bm_vendor_pwm_esp32_idf.c` | `bm_pwm_driver_api` × 2（M0 / M1），ESP-IDF MCPWM |
| `bm_vendor_adc_esp32_idf.c` | `bm_adc_driver_api` × 2（M0 / M1），ADC1 + 缓存采样 |
| `bm_vendor_encoder_esp32_idf.c` | `bm_encoder_driver_api` × 2（M0 / M1），双路 I2C AS5600 |

对应导出实例（声明进同名 `.h`，供上层应用 `extern` 引用）：
- `bm_hal_pwm_m0` / `bm_hal_pwm_m1`
- `bm_hal_adc_m0` / `bm_hal_adc_m1`
- `bm_hal_encoder_m0` / `bm_hal_encoder_m1`

### 2.3 CMakeLists.txt（修改）

```
portable/vendor/esp32_idf/CMakeLists.txt
```

把新增 `.c` 加入 `ESP_PLATFORM` 分支的 `idf_component_register SRCS` 和 `else` 分支的 `add_library` 两处。

---

## 3. 驱动契约（不可改，必须实现）

契约头位于 `include/drv/`，参照实现见 `portable/sim/native/`。

### 3.1 `bm_pwm_driver_api`

```c
struct bm_pwm_driver_api {
    int (*set_duty)(const struct bm_hal_pwm *dev, uint32_t phase, uint16_t duty);
    int (*enable_outputs)(const struct bm_hal_pwm *dev, int enable);
    int (*request_safe_state)(const struct bm_hal_pwm *dev);
    int (*bind_update)(const struct bm_hal_pwm *dev, const bm_hal_hrt_binding_t *binding);
};
```

- `set_duty`：`phase` = 0/1/2 对应 A/B/C 相，`duty` 范围 `[0, BOARD_FOC_PWM_MAX(1000)]`，写入 MCPWM comparator。
- `enable_outputs`：`enable=1` 拉高 M_EN（GPIO12），`enable=0` 拉低，同时三相占空比置 0。
- `request_safe_state`：三相占空比立即置 0 + 拉低 M_EN，ISR 安全。
- `bind_update`：将 MCPWM 周期事件（每 PWM 周期 ISR）注册到 `bm_hal_hrt_binding_t`，驱动电流环定时触发。

### 3.2 `bm_adc_driver_api`

```c
struct bm_adc_driver_api {
    int (*read_injected)(const struct bm_hal_adc *dev, uint32_t rank, uint16_t *value);
    int (*bind_complete)(const struct bm_hal_adc *dev, const bm_hal_hrt_binding_t *binding);
};
```

- `read_injected(rank=0)`：返回 ia 最近缓存原始 ADC 值；`rank=1` 返回 ib。
- `bind_complete`：ADC 采样完成时触发 binding（在 PWM ISR 里完成采样后调用）。

**重要架构取舍：经典 ESP32 ADC1 无 STM32 那种硬件 PWM 触发注入通道。**  
采用「MCPWM 周期 ISR 内软件触发 ADC1 单次采样」近似同步方案：
1. MCPWM 周期 ISR 触发 → 调用 `adc1_get_raw()` 依次读 ia / ib → 存入 per-motor 缓存 → 调用 `bind_complete` binding → 电流环开始计算。
2. `read_injected` 直接返回缓存值（不再触发新采样）。
3. 近似误差：ADC 读取约 20~50µs，相对 50µs 电流环周期有误差，实机须标定电流相偏置。

### 3.3 `bm_encoder_driver_api`

```c
struct bm_encoder_driver_api {
    int (*read)(const struct bm_hal_encoder *dev, int32_t *value);
};
```

- `read`：通过 I2C 读 AS5600 寄存器 `0x0C~0x0D`（RAW ANGLE，12 bit），返回 `[0, 4095]`，上层做弧度换算。
- 两路 I2C 独立初始化（`i2c_master_bus_create` 各一条总线）。
- I2C 速率建议 400 kHz（Fast Mode）。

---

## 4. 电气/时序常量（放进引脚头，沿用 `BOARD_FOC_*` 前缀）

| 宏名 | 值 | 说明 |
|------|----|------|
| `BOARD_FOC_PWM_HZ` | `20000u` | PWM 载波频率（中心对齐） |
| `BOARD_FOC_CURRENT_HZ` | `10000u` | 电流环触发频率（每 PWM 周期触发一次） |
| `BOARD_FOC_PWM_MAX` | `1000u` | 满量程 comparator 值 |
| `BOARD_FOC_VBUS_V` | `24.0f` | 标称母线电压（V） |
| `BOARD_FOC_VBUS_DIV` | `8.5f` | 母线分压比（实测后校准） |
| `BOARD_FOC_CURRENT_ADC_SCALE` | `1000.0f` | 电流 ADC 标定比例（占位，实机校准后更新） |
| `BOARD_FOC_POLE_PAIRS` | `7.0f` | **需按电机实物确认**，此处为占位值 |
| `BOARD_FOC_ENCODER_CPR` | `4096u` | AS5600 RAW ANGLE 12bit → 4096 counts/rev |

---

## 5. 执行步骤

```
Step 1  填充 bm_hal_instances_esp32wroom32e.h
        ↳ 所有引脚宏 + 时序常量

Step 2  实现 bm_vendor_pwm_esp32_idf.c / .h
        ↳ MCPWM 初始化（中心对齐，20kHz）
        ↳ set_duty / enable_outputs / request_safe_state
        ↳ bind_update（注册 MCPWM 周期 ISR → bm_hal_hrt_binding）
        ↳ 导出 bm_hal_pwm_m0 / m1

Step 3  实现 bm_vendor_adc_esp32_idf.c / .h
        ↳ ADC1 初始化（4 通道：CH3/0 M0，CH7/6 M1）
        ↳ 在 PWM ISR 回调里软触发采样 + 缓存 + 调用 bind_complete
        ↳ read_injected 返回缓存值
        ↳ 导出 bm_hal_adc_m0 / m1

Step 4  实现 bm_vendor_encoder_esp32_idf.c / .h
        ↳ 双路 I2C master bus 初始化
        ↳ read() 读 AS5600 RAW ANGLE 寄存器 0x0C~0x0D
        ↳ 导出 bm_hal_encoder_m0 / m1

Step 5  修改 CMakeLists.txt
        ↳ ESP_PLATFORM 分支：idf_component_register SRCS 加入三个 .c
        ↳ else 分支：add_library 同步加入

Step 6  验证
        ↳ 函数表无 NULL 成员检查
        ↳ 引脚宏值与本文对齐检查
        ↳ cmake 配置 / 编译烟雾（见§6）
        ↳ native_sim 后端不受影响确认
```

---

## 6. 验收标准

### 6.1 函数表完整性
三个 `driver_api` struct 的所有函数指针非 NULL，两电机各有独立实例。

### 6.2 引脚宏一致性
`bm_hal_instances_esp32wroom32e.h` 中的 GPIO 值与第 1 节表格完全一致。

### 6.3 配置/编译（ESP-IDF 可用时）
```bash
# 安装并 export ESP-IDF v5.x 后：
cmake -B build_esp32 -S . \
  -DBM_BUILD_TESTS=OFF \
  -DBM_BACKEND=sdk_esp32_idf \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-xtensa-esp32.cmake
# 期望：配置成功，无未解析符号警告
```

ESP-IDF 不可用时，退化为：
```bash
# 烟雾脚本
python tests/cmake/check_esp32_backend_configure.py
# 或 GCC 语法检查（仅结构合法性）
gcc -fsyntax-only -I include -I include/bm/common \
    -DBM_DRV_HAS_BACKEND portable/vendor/esp32_idf/bm_vendor_pwm_esp32_idf.c
```

### 6.4 不破坏现有后端
未改动公共头和 native/stm32 路径，`BM_BACKEND=native_sim` 构建不受影响。

---

## 7. 已知约束与后续工作

| 项 | 说明 | 后续 |
|----|------|------|
| ADC 同步近似 | MCPWM ISR 软触发，非硬件同步；采样时刻有抖动 | 实机标定电流相偏置；或引入外部 ADC（SPI） |
| ADC2 WiFi 互斥 | VIN_MEA(GPIO13) 用 ADC2，WiFi 启用时失效 | 若需 WiFi+母线监测，换 ADC 外挂或电阻分压接 ADC1 脚 |
| 极对数占位 | `BOARD_FOC_POLE_PAIRS=7.0f` 为占位值 | 按实际电机型号确认（灯哥板常见 7 对极，但须确认） |
| 电流标定 | `BOARD_FOC_CURRENT_ADC_SCALE` 为 1000.0f 占位 | 实机接已知负载标定 INA240 增益 + ADC 参考 |
| AS5600 CS 脚 | I2C 模式下 CS 接 GND，GPIO22/21 引脚宏预留不驱动 | 若换 SPI 编码器改用这两脚 |
| RGB 驱动 | GPIO2 宏预留，WS2812B 驱动未实现 | 可用 ESP-IDF RMT 外设实现，独立扩展 |
| motor_foc 组件成熟度 | 当前为 E1（native_sim 验证），实机电流环未 PIL 验证 | 按 `board/sdk_stm32g4_CHECKLIST.md` 逻辑补 ESP32 版实机清单 |

---

## 8. 相关文档

| 主题 | 链接 |
|------|------|
| ESP-IDF 接入总览 | [08-ESP-IDF与灯哥平衡车集成](08-ESP-IDF与灯哥平衡车集成.md) |
| HAL 契约与移植要点 | [01-HAL契约与移植要点](01-HAL契约与移植要点.md) |
| HRT 绑定模型 | [docs/05-API参考/bm_hrt.md](../05-API参考/bm_hrt.md) |
| 驱动契约头 | `include/drv/bm_drv_pwm.h` / `bm_drv_adc.h` / `bm_drv_encoder.h` |
| native 参照实现 | `portable/sim/native/bm_drv_pwm_native.c` 等 |
| 板级电气常量习惯 | `board/bm_board_envelope_stm32g4.h`（格式参考） |
