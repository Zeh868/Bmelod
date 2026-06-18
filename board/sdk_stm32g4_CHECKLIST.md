# sdk_stm32g4 实机验收清单（M5 脚手架）

与 `bm_board_envelope_stm32g4.h`、`WCET_REPORT_TEMPLATE.md` 及 `tools/board/build_stm32g4_motor_foc.ps1` 配套使用。

## 1. 环境

- [ ] arm-none-eabi-gcc 已安装并在 PATH
- [ ] STM32CubeG4 路径已设置（`BM_STM32_CUBE_PATH` 或脚本 `-Stm32CubePath`）
- [ ] 目标器件与 Nucleo/自定义板一致（默认 `STM32G474xx`）
- [ ] ST-Link / OpenOCD 可连接目标

## 2. 构建

```powershell
.\tools\board\build_stm32g4_motor_foc.ps1 -Stm32CubePath C:\STM32CubeG4
```

- [ ] CMake 配置无错误（`BM_BACKEND=sdk_stm32g4`）
- [ ] `motor_foc_sensored` 链接成功
- [ ] 生成 `.elf` / `.hex` 可烧录

## 3. 板级与 HAL

- [ ] `board/bm_board_envelope_stm32g4.h` 已按硬件填写 R、极对数、CPR、PWM/采样频率
- [ ] `portable/vendor/stm32g4/bm_hal_instances_stm32g4.h` TIM/ADC/ENC 实例已绑定
- [ ] 电流采样极性与 Clarke/Park 链一致（示波器或开环阶跃验证）
- [ ] PWM 死区、ADC 触发窗口与 `motor_current_sense` 配置一致

## 4. WCET

- [ ] 电流环周期预算已定义（见 `BOARD_WCET_*` 占位宏）
- [ ] 示波器/GPIO 翻转或 DWT 实测各段 WCET
- [ ] 填写 [WCET_REPORT_TEMPLATE.md](WCET_REPORT_TEMPLATE.md)
- [ ] 实测 WCET ≤ 环周期 × 建议安全系数（≤ 70%）

## 5. PIL / 指标

- [ ] 空载/轻载电流环跟踪误差在预期范围
- [ ] 编码器方向与速度符号正确
- [ ] 故障注入（过流/欠压占位）进入安全态
- [ ] 与 native_sim / QEMU 行为差异已记录

## 6. 签署

| 项 | 填写 |
|----|------|
| 板型 | |
| 固件版本 / commit | |
| 验收人 | |
| 日期 | |
