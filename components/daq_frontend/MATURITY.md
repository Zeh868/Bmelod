# daq_frontend 成熟度

Maturity: E1 - 前期应用探索

Validated: float32 / native_sim / Unity `test_daq_frontend` 的初始化、触发、预触发快照、
后触发采集、RMS 与波峰因数行为

Not validated: 实机 ADC/DMA 连续采样、并发生产者、多核、WCET 与长期稳定性

## 范围

- 单轴 DAQ 前端的 arm/feed 流式采样接口
- 阈值触发、预触发环形缓冲和定长后触发采集
- RMS 滑窗、峰值与波峰因数统计
- 按时间顺序导出预触发快照

## 依赖

- 使用应用静态提供的样本缓冲，运行期不分配堆内存
- 归属 `bm_component_diagnostics`，依赖 `bm_algorithm` 与公共组件遥测契约

## 已知限制

- 尚未验证 ADC/DMA ISR 与主循环并发接入
- 尚无目标 MCU 上的采样吞吐、WCET 和缓存一致性证据
- 当前成熟度不承诺多通道同步采集或多核共享实例

## E2 晋级条件

- 在至少一种目标 MCU 的 ADC/DMA 链路上完成持续采样与触发验证
- 补充采样频率上界、缓冲容量和 WCET 证据
- 明确 ISR/主循环交接方式并覆盖溢出、并发和长期运行场景
