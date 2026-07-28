# estop_input 成熟度

Maturity: E1 - 前期应用探索

Validated: native_sim / 消抖后激活 / 锁存与清除 / 虚拟时间推进

Not validated: 真实急停按钮、EMC 干扰、STM32G4 EXTI 后端

## 范围

- `bm_estop_input_init` / `_reset` / `_clear_latch` / `_active` / `_latched` / `_poll`
- 强制消抖（stable_us 须 > 0）
- 支持 active_low/active_high

## 已知限制

- STM32G4 后端 EXTI 实现当前为桩
- 未集成安全继电器/双通道逻辑
