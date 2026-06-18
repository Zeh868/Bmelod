# bms_estimation 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / 库仑积分 + 静置 OCV 融合 + 温度容量修正

Not validated: 实机 Pack 拓扑、多电芯不一致、休眠电流、SOH、均衡

## 范围

- `bm_bms_estimation_step`：Pack 电流/电压/温度采样回调
- K0：`bm_algo_coulomb`、`bm_algo_ocv`、`bm_algo_soc_fusion`、温度补偿

## 已知限制

- 单 Pack 聚合模型，无电芯级诊断
- OCV 融合仅在静置电流低于阈值且超时后启用
- 温度补偿为一阶线性模型
