# tmc2209 成熟度

Maturity: E1 - 前期应用探索

Validated: native_sim 假 UART dev / 读写帧格式 / CRC8 / IOIN 通讯校验（含版本域 0x21）/ IHOLD_IRUN、CHOPCONF.MRES 读写 / SG_RESULT 堵转沿上报 / IFCNT 写确认 / GSTAT 读清 / DRV_STATUS 故障解析 / StealthChop·SpreadCycle 切换 / 连续通讯失败离线

Not validated: 实机 TMC2209 通讯（单线时序/应答延迟）、多从机总线、电流实际输出标定、PWMCONF 细调

## 范围

- Trinamic 单线 UART 协议（8B 写帧 / 4B 读请求 + 8B 应答，CRC8 多项式 0x07 初值 0）
- `bm_tmc2209_init`（IOIN 往返校验 + 可选 GSTAT 读清）/ `write_reg`（IFCNT 写确认）/ `read_gstat` / `clear_gstat` / `read_drv_status` / `set_chopper_mode` / `set_microsteps` / `set_current` / `read_stallguard` / `poll`（堵转沿 → resources.stall_callback，业务可转 health_monitor）
- 架在 bm_hal_uart_dev 实例上；单线回环丢弃由 config.single_wire 控制

## 已知限制

- 应答接收为有界重试轮询（`config.rx_retries`，0=缺省 200），字节间超时按固定窗口计数，实机按波特率核对；未使用 `bm_uptime_us` 做字节级超时
- 写确认依赖 IFCNT 递增，重试次数由 `config.write_retries`（0=缺省 3）控制
- 连续通讯失败达 `config.offline_threshold`（0=缺省 5）后置 `state.offline` 并清 `comm_ok`；需重新 `init` 恢复
- StealthChop 切换仅写缺省 `BM_TMC2209_PWMCONF_DEFAULT`，App 可按工况再细调
- 默认值零内置：电流/细分全部由业务显式设置
