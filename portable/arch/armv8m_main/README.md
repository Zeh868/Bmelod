# armv8m_main — Cortex-M33 / M55 / M85

临界区与内存屏障与 `armv7em` 相同（primask + basepri）。本目录仅区分 **Arch ID** 与 TrustZone 集成说明。

## TrustZone（非安全域 Port）

- 框架 `bm_drv_critical_api` 假定运行在 **非安全（Non-Secure）** 世界；`primask` / `basepri` 仅屏蔽非安全侧 IRQ。
- 若应用需从非安全域调用安全服务，由 **vendor** 提供 `SG`/`CMSE` 封装；arch 层不包含 `TZ` 寄存器操作。
- 安全域固件应使用独立 `bm_port.c` 实例或 `BM_PORT_PROFILE_ULTRA_ONLY` 裁剪。

## CMake

```cmake
set(BM_PORT_ARCH armv8m_main)
bm_port_add_arch(armv8m_main _tgt)
target_link_libraries(my_fw PRIVATE ${_tgt})
```
