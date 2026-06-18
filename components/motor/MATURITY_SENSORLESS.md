# motor_foc_sensorless E1 成熟度声明

**Component**: `motor_foc_sensorless`  
**Version**: 0.2  
**Backend**: float32  
**Platform**: native_sim only  
**Maturity**: E1（前期应用探索）

## 已验证

- 启动状态机：IDLE → ALIGN → OPEN_LOOP → OBSERVER（E1 骨架）
- 磁链观测器 + PLL 角度更新（K0 `bm_algo_flux_observer_step`）
- MTPA / 弱磁 id 参考修正（K0）
- 电流环 + SVPWM 编排（无编码器反馈路径）
- `test_motor_foc_sensorless.c` 冒烟与状态迁移

## 未验证

- 实机 HAL、ADC 极性、死区与采样窗口
- 低速/过零/顺风启动/反转与再捕获
- WCET、过调制、参数温漂与母线跌落全包络
- 观测器失锁后的安全恢复策略（当前 latch FAULT）

## 状态机（E1）

| 阶段 | 行为 |
|------|------|
| IDLE | 零电压；ENABLE 后进入 ALIGN |
| ALIGN | 固定 θ=0，施加 align_id 电流 |
| OPEN_LOOP | θ 积分 + ω 斜坡，固定 iq_ref |
| OBSERVER | 磁链观测闭环；ω 过低超时 → FAULT |
| FAULT | latch_fault，保持安全态 |

## 不宜用于

- 量产电调或无感启动产品（需 V2/I3 证据）
- 未声明硬件包络下的安全关键应用
