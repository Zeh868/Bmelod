# SEGGER RTT（第三方）

本目录为 [SEGGERMicro/RTT](https://github.com/SEGGERMicro/RTT) 源码快照（BSD-3-Clause），供 `bm_console` RTT 后端使用。

## 启用

CMake 选项 `BM_ENABLE_SEGGER_RTT=ON`（`sdk_esp32_idf` 后端默认开启）后：

- 编译 `RTT/SEGGER_RTT.c` 为静态库 `bm_segger_rtt`
- `bm_hal` 定义 `BM_CONSOLE_HAS_SEGGER_RTT`
- `bm_console_rtt.c` 调用 `SEGGER_RTT_Write` / `SEGGER_RTT_Read`

`native_sim` / `native_sim_mp` 单测默认 **不** 链接真 RTT，仍用内置仿真缓冲。

## 配置

在 `Config/SEGGER_RTT_Conf.h` 中调整上行/下行缓冲区等参数；勿修改 `RTT/SEGGER_RTT.c` 以保持 J-Link 协议兼容。

## 目标板

1. `bm_config.h` 中设置 `BM_CONFIG_CONSOLE_LOG_BACKEND = BM_CONSOLE_BACKEND_RTT`
2. J-Link 连接目标，`RTT Viewer` 或 `JLinkRTTClient` 查看 Channel 0

## 升级

从上游 release 同步 `RTT/` 与 `Config/SEGGER_RTT_Conf.h`，保留本 README 与 CMake 路径约定。
