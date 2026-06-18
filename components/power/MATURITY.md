# power_control 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / Buck 电压环 + 电流环 + 软启动斜坡

Not validated: 实机 CCM/DCM、PFC/LLC、保护链、EMI、多相均流

## 范围

- `bm_power_control_voltage_step` / `bm_power_control_current_step`
- K0：双 PI、输出限幅、`bm_algo_ramp` 软启动

## 已知限制

- 抽象 Buck 拓扑，plant 由应用/Demo 提供
- 无输入/输出过压过流硬件保护逻辑
- 电压环与电流环频率比由应用槽周期决定
