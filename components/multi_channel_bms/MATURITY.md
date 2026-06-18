# multi_channel_bms 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / QEMU / 16 路电芯 LPF1 滤波 + Pack 电压/SOC 简易指标

Not validated: 实机 ADC 标定、电芯不一致、真实 OCV 表、均衡与故障注入

## 范围

- Pack 级硬件槽多通道 ADC 轮询
- 16 路 cell 级 `bm_exec` 与 GPIO 资源声明
- `bm_algo_lpf1` 截止 5 Hz 滤波
- 滤波后过压检测（> 4200 mV）
- Pack 级 avg/min/max 电压与线性 SOC 占位（3000–4200 mV 映射）

## 已知限制

- SOC 为单电芯 OCV 线性占位，非 `bm_algo_ocv` / EKF
- 无库仑积分、温度补偿与静置融合
- plant 电压为仿真递增值（3300 + ch mV），非真实 BMS 硬件
