# input_debounce 成熟度

Maturity: E1 - 前期应用探索

Validated: native_sim / 时间阈值消抖 / 抖动抑制 / 稳定沿事件 / NULL 安全

Not validated: 实机抖动环境、亚微秒级时间精度

## 范围

- `bm_input_debounce_update` / `_filtered` / `_is_stable`
- 固定稳定时间阈值，输入须在 `stable_us` 内不变才视为有效沿

## 已知限制

- 当前仅支持单稳时间阈值，未支持多数决/计数器滤波
- 时间戳依赖 `bm_uptime_us()`，精度由后端决定
