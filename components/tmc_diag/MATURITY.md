# tmc_diag 成熟度

Maturity: E1 - 前期应用探索

Validated: native_sim / active_low/active_high 配置 / EXTI 触发 / 锁存

Not validated: 真实 TMC2209 DIAG 引脚时序、STM32G4 EXTI 后端

## 范围

- `bm_tmc_diag_init` / `_reset` / `_clear_latch` / `_active` / `_latched`
- 监听 DIAG 引脚，激活电平时沿触发并锁存
- 组件只上报事件，不停机

## 已知限制

- STM32G4 后端 EXTI 实现当前为桩
- 未与 TMC2209 组件内部状态联动
