# motor_foc_sensored 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / 双环 FOC + 编码器速度反馈

Not validated: 实机 HAL、电流采样极性校准、WCET、多轴、弱磁

板级包络模板见 `Demo/motor_foc_sensored/board/bm_board_envelope_stm32g4.h`。

## 范围

- 电流环 10 kHz + 速度环 1 kHz（Scheduled HRT）
- `bm_algorithm` Clarke/Park、SVPWM、PI、编码器、ramp
- 命令/遥测经 snapshot + `bm_module` 监督层

## 已知限制

- native_sim 电流反馈可经 `sim_fb` 注入；ADC 链极性待实机标定
- 电角度在快环使用慢环更新的 `encoder.position_rad`，支持方向与零偏校准
- 无硬件过流、无弱磁、无 MTPA
