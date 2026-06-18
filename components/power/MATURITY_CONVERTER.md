# power_converter 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / Buck 峰值电流模式（电流参考斜坡 + 电流 PI）

Not validated: 实机 CCM/DCM、斜坡补偿、Boost/PFC/LLC、保护链、EMI

## 范围

- `bm_power_converter_current_step`
- K0：`bm_algo_ramp` 电流软启动 + `bm_algo_pi` 电流环

## 已知限制

- 单电流环骨架，电压/功率外环由应用层提供
- 无逐周期限流与硬件比较器接口
- plant 与 PWM 由应用/Demo 注入
