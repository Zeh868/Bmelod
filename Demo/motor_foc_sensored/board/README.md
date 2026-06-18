# motor_foc_sensored 板级移植（STM32G4）

## 职责划分

| 侧 | 内容 |
|----|------|
| **框架** | `motor_foc_sensored` 组件、`Demo/motor_foc_sensored`、native_sim 验收 |
| **用户** | `board/bm_board_envelope_stm32g4.h` 参数、`sdk_stm32g4` HAL 实例、ADC/PWM/编码器标定 |

## 构建（需 STM32CubeG4）

```bash
cmake -B build/demo/board/motor_foc -S Demo/motor_foc_sensored \
  -DBM_BACKEND=sdk_stm32g4 \
  -DBM_STM32_CUBE_PATH=/path/to/STM32CubeG4 \
  -DBM_STM32_DEVICE=STM32G474xx \
  -DBM_ENABLE_ALGORITHM=ON \
  -DBM_ENABLE_COMPONENT_MOTOR=ON \
  -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-arm-none-eabi.cmake
```

## 移植检查清单

1. 填写 `bm_board_envelope_stm32g4.h` 中 R、极对数、CPR、PWM/采样频率
2. `portable/vendor/stm32g4/bm_hal_instances_stm32g4.h` 绑定 TIM/ADC/ENC
3. 电流采样极性 → Clarke/Park 链标定（或暂时保留 `sim_fb` 仅用于台架）
4. 运行 `motor_foc_sensored` 实机版，记录 WCET 与电流/速度跟踪指标

## 成熟度

当前组件为 **E1**（native_sim）。实机 V2 需 PIL/HIL 证据，见 `components/motor/MATURITY.md`。
