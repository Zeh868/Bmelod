# motion_coordination 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / 最多 4 轴独立斜坡协调

Not validated: 同步梯形/S 曲线、龙门轴约束、实机伺服

## 范围

- `bm_motion_coordination_set_targets` + 各轴 `bm_algo_ramp`

## 已知限制

- 各轴独立斜坡，无主轴从轴几何约束
- 固定最大 4 轴
