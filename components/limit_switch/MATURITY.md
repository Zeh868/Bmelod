# limit_switch 成熟度

Maturity: E1 - 前期应用探索

Validated: native_sim / GPIO EXTI 触发 / 无消抖直接回调 / 锁存与清除 / NULL 安全

Not validated: 实机限位开关机械抖动、STM32G4 EXTI 后端真机时序

## 范围

- `bm_limit_switch_init` / `_reset` / `_clear_latch` / `_triggered` / `_latched` / `_poll`
- 支持配置消抖时间（stable_us > 0 时由 poll 完成消抖）
- 组件只上报事件，不执行急停/回退动作

## 已知限制

- STM32G4 后端 EXTI 实现当前为桩（返回 BM_ERR_NOT_SUPPORTED）
- 未集成 health_monitor 遥测（待后续打通）
