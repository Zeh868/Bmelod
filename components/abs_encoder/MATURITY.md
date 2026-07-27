# abs_encoder 成熟度

Maturity: E1 - 前期应用探索

Validated: native_sim 假 SPI / AS5047P 命令帧与偶校验 / 流水读 / 错误位与 MAGH/MAGL 状态映射 / 校验错误拒绝

Not validated: 实机 AS5047P 读数（SPI 时序/磁场安装）、其它型号 vtable、温度/AGC 补偿

## 范围

- 型号 vtable `bm_abs_encoder_api_t{read_angle, read_status}` + 业务聚合设备 `{api, config}`
- AS5047P 实现：16bit 帧（bit15 偶校验、bit14 R/W 或错误位）、14bit 角度（ANGLECOM 0x3FFF）、诊断（DIAAGC 0x3FFC，MAGH/MAGL）
- 架在 bm_hal_spi 上；CS 管理由 bm_spi_config_t 承担

## 已知限制

- 状态字为组件统一映射（bit15=错误、bit1=MAGH、bit0=MAGL），非器件原始寄存器
- 每读一次两帧流水（命令+NOP），E1 未做连续流水复用
- 其它型号（MT6816/MA730 等）待按 vtable 追加
