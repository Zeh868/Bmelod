# rs485 成熟度

Maturity: E1 - 前期应用探索

Validated: native_sim / DE 方向控制（高有效）/ pre/post delay 状态机 / 回显过滤（含跨 DMA 事件）/ 半双工冲突检测（含 TX_PRE）/ 帧长上限丢帧 / UART 错误粘滞位去重 / 复位回 RX / 链路统计

Not validated: 真实 RS485 收发器总线时序、STM32G4 USART 后端、硬件自动 DE、TX 超时回退真机行为

## 范围

- `bm_rs485_init` / `_reset` / `_send` / `_poll` / `_get_stats` / `_dir`
- 半双工 DE 方向控制、发送前后保持时间、RX 帧事件、冲突检测、TX 超时回退（`config.tx_timeout_us`，0 不检测）、链路统计
- 帧拼装在独立内部缓冲（上限 `BM_RS485_MAX_FRAME_LEN`=256）进行；`config.rx_buf` 专职 HAL 环形存储
- 组件只上报事件与统计，App 决定超时/冲突后的业务动作

## 已知限制

- 无 `bm_rs485_exec_ops`（未接入 bm_exec 生命周期表）
- 遥测/事件未走 `BM_COMPONENT_PUBLISH_TELEMETRY` 宏，经 resources 回调直接上报
- 帧长上限固定 256 字节，与 App 外部环形缓冲大小无关；超长帧直接丢弃
- STM32G4 后端真机未验证
