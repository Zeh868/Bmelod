# stepper_pulse 成熟度

Maturity: E1 - 前期应用探索

Validated: native_sim / 步进计数（有符号含方向）/ 方向建立时间 / 脉冲频率钳制 / stop 位置保持 / 假定时器集成（test_stepper_servo 位置收敛）

Not validated: 实机脉冲时序（抖动/最小脉宽）、高频（>10kHz）CPU 占用、丢步检测

## 范围

- `bm_stepper_pulse_set_velocity` / `_stop` / `_position` / `_on_timer`（平台定时器入口）
- 芯片无关：STEP/DIR 电平与定时器武装全部经 resources 回调；arm_timer 语义为“到期时间上限”（平台剩余更短不重设）

## 已知限制

- ISR 翻转方式建议 ≤10kHz；更高频率的 CPU 占用与时序抖动须实机评估
- 定时器设备实例契约未建（实机由业务/vendor 绑一路 TIM，登记为缺口）
- 无加减速规划（由 motion_profile/control_loop 在环层提供）
