# fault_derating 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / 故障锁存 + 线性降额 + 恢复定时

Not validated: 多级降额曲线、实机故障证据快照、WCET

## 范围

- `bm_fault_derating_latch`：触发锁存并沿降额曲线斜坡下降
- `bm_fault_derating_clear_request`：清除锁存后计时恢复满功率
- K0：`bm_algo_ramp`

## 已知限制

- 单通道降额因子，无分通道限幅
- 恢复为固定超时，无外部确认信号
