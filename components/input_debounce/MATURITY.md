# input_debounce 成熟度

Maturity: E1 - 前期应用探索

Validated: native_sim / 时间阈值消抖 / 抖动抑制 / 稳定沿事件 / NULL 边界安全

Not validated: 实机抖动环境、亚微秒级时间精度

## 范围

- `bm_input_debounce_update` / `_filtered` / `_is_stable`
- 配置、状态与纯时间滤波算法下沉至 `bm/common/bm_input_debounce.h`
- 固定稳定时间阈值，输入须在 `stable_us` 内不变才视为有效沿

## 已知限制

- 当前仅支持单稳时间阈值，未支持多数决/计数器滤波
- 时间戳依赖 `bm_uptime_us()`，精度由后端决定
- 未提供 exec_ops（被 limit_switch / estop_input 内嵌使用，待后续按需接入 bm_exec）
- 未接遥测宏 `BM_COMPONENT_PUBLISH_TELEMETRY`（待后续）
