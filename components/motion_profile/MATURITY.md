# motion_profile 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / 单轴梯形与 S 曲线 goto

Not validated: 多轴协调、在线重规划、实机伺服

## 范围

- `bm_motion_profile_goto` + `bm_algo_trapezoid` / `bm_algo_scurve`

## 已知限制

- 单轴、无联锁与限位
- S 曲线为 jerk 受限加速度跟踪，非严格七段解析
