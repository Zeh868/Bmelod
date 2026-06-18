# armv8m_base — Cortex-M23

与 `armv6m` 共用 primask 临界区（无 `basepri` 优先级掩码）。`BM_HAL_HAS_PRIORITY_MASK` 若启用则 `enter_below` 退化为全关中断。

CMake 目标 `bm_port_arch_armv8m_base` 为 `bm_port_arch_armv6m` 的别名。
