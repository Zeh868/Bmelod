# tmc2209 成熟度

Maturity: E1 - 前期应用探索

Validated: native_sim 假 UART dev / 读写帧格式 / CRC8 / IOIN 通讯校验 / IHOLD_IRUN、CHOPCONF.MRES 读写 / SG_RESULT 堵转沿上报

Not validated: 实机 TMC2209 通讯（单线时序/应答延迟）、多从机总线、电流实际输出标定

## 范围

- Trinamic 单线 UART 协议（8B 写帧 / 4B 读请求 + 8B 应答，CRC8 多项式 0x07 初值 0）
- `bm_tmc2209_init`（IOIN 往返校验）/ `set_microsteps` / `set_current` / `read_stallguard` / `poll`（堵转沿 → resources.stall_callback，业务可转 health_monitor）
- 架在 bm_hal_uart_dev 实例上；单线回环丢弃由 config.single_wire 控制

## 已知限制

- 应答接收为有界重试轮询，字节间超时按 BM_TMC2209_RX_RETRIES 固定窗口，实机按波特率核对
- 无写后读回校验（写帧无应答属协议特性）
- 默认值零内置：电流/细分全部由业务显式设置
