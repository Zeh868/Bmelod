# sdk_stm32g4 实机验收清单（M5 脚手架）

与 `bm_board_envelope_stm32g4.h`、`WCET_REPORT_TEMPLATE.md` 配套使用。
Port 总览见 [`docs/03-移植与IDE集成/10-STM32G4-Port实机.md`](../docs/03-移植与IDE集成/10-STM32G4-Port实机.md)。

## 0. 冒烟基线（ATK-DMG474，2026-07-30 已通过）

- [x] arm-none-eabi-gcc + Ninja 可构建 `board/atk_dmg474_smoke`
- [x] `BM_STM32_CUBE_PATH` 指向外置 Cube（如 `D:\Code\Bmelod-sdks\stm32\STM32CubeG4`）
- [x] 目标器件 STM32G474VET6；J-Link SWD 可连接
- [x] 烧写 `.elf` 后 RTT Viewer Channel 0 可见心跳

未覆盖项（仍属后续）：HSE/PLL 170MHz、RS485/FDCAN/NVS、电机环 WCET。

## 1. 环境

- [x] arm-none-eabi-gcc 已安装并在 PATH（冒烟机已验）
- [x] STM32CubeG4 路径已设置（`BM_STM32_CUBE_PATH`）
- [ ] 目标器件与 Nucleo/自定义板引脚表一致（电机工程须覆盖 instances）
- [x] 调试器可连接目标（J-Link；ST-Link 亦可烧写，RTT Viewer 需 J-Link）

## 2. 构建

冒烟（推荐起点）：

```powershell
.\tools\board\build_atk_dmg474_smoke.ps1
```

- [x] CMake 配置无错误（`BM_BACKEND=sdk_stm32g4`）
- [x] `atk_dmg474_smoke` 链接成功并生成 `.elf` / `.bin`
- [ ] 电机 / 全功能 Demo 链接成功（脚本 `build_stm32g4_motor_foc.ps1` 已下线，待重建）

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
