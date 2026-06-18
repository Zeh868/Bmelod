# grid_control 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / SOGI-PLL + PR 电流环骨架

Not validated: dq 变换、并网保护、实机逆变器

## 范围

- 电网电压 SOGI-PLL 锁相
- 单相 PR 电流跟踪（标量占位）

## 已知限制

- 无电压环与功率因数控制
- 三相系统未展开
