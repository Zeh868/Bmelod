# estop_input 成熟度

Maturity: E1 - 前期应用探索

Validated: native_sim / 消抖后激活 / active_low 低有效触发 / 锁存与清除 / 虚拟时间推进 / NULL 边界安全

Not validated: 真实急停按钮、EMC 干扰

## 范围

- `bm_estop_input_init` / `_reset` / `_clear_latch` / `_active` / `_latched` / `_poll`
- 强制消抖（stable_us 须 > 0）
- 支持 active_low/active_high
- poll-only：不注册 EXTI，不占用 EXTI 线（2026-07-28 起移除空操作 EXTI 注册）

## 已知限制

- 未提供 `bm_estop_input_exec_ops`（周期组件待接入 bm_exec，待后续）
- 未接遥测宏 `BM_COMPONENT_PUBLISH_TELEMETRY`（待后续）
- 未集成安全继电器/双通道逻辑
