# limit_switch 成熟度

Maturity: E1 - 前期应用探索

Validated: native_sim / GPIO EXTI 触发（stable_us==0 直接回调）/ FALLING 低电平有效（常闭开关）/ poll 消抖路径 / 锁存与清除 / NULL 边界安全

Not validated: 实机限位开关机械抖动、STM32G4 EXTI 后端真机时序

## 范围

- `bm_limit_switch_init` / `_reset` / `_clear_latch` / `_triggered` / `_latched` / `_poll`
- 支持配置消抖时间（stable_us > 0 时由 poll 完成消抖，EXTI 回调不改写状态）
- 有效电平由 EXTI 沿推导：FALLING 单独配置为低电平有效，RISING/BOTH 为高电平有效
- 事件计数语义为消抖后事件计数（ISR 原始边沿不计数）
- 组件只上报事件，不执行急停/回退动作

## 已知限制

- STM32G4 后端 EXTI 实现当前为桩（返回 BM_ERR_NOT_SUPPORTED）
- 未提供 `bm_limit_switch_exec_ops`（周期组件待接入 bm_exec，待后续）
- 未接遥测宏 `BM_COMPONENT_PUBLISH_TELEMETRY`（待后续）
- 未集成 health_monitor 遥测（待后续打通）
