/**
 * @file bm_config_rv64_icount.h
 * @brief QEMU RISC-V64 icount 确定性测试专用 bm 配置
 *
 * 单核（-smp 1）RISC-V64 virt 环境下运行 L2 确定性验证测试套件
 * （probe 标定 / clarke Δ==0 / bus LATEST 同相位 Δ==0）。镜像 ARM 线
 * bm_config_icount.h：单核、关闭日志/IPC 噪声、最小事件系统。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-07-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-01       0.1            zeh            初稿（Task 7 RV64 icount 骨架）
 *
 */
#ifndef BM_CONFIG_RV64_ICOUNT_H
#define BM_CONFIG_RV64_ICOUNT_H

/* icount 测试仅单核 */
#define BM_CONFIG_CPU_COUNT          1u

/* 关闭日志/IPC，减少串口噪声 */
#define BM_CONFIG_ENABLE_LOG         0
#define BM_CONFIG_ENABLE_IPC         0

/* 核心事件子系统（最小配置，与 ARM 线一致：
 *   EVENT_QUEUE_SIZE % EVENT_PRIORITIES == 0，且商为 2 的幂：4/2=2） */
#define BM_CONFIG_MAX_EVENT_TYPES          4
#define BM_CONFIG_MAX_EVENT_SUBSCRIBERS    4
#define BM_CONFIG_EVENT_QUEUE_SIZE         4
#define BM_CONFIG_EVENT_PRIORITIES         2
#define BM_CONFIG_EVENT_PRIORITY_BURST_MAX 4u
#define BM_CONFIG_EVENT_INLINE_DATA_SIZE   8
#define BM_CONFIG_MAX_MODULES              2
#define BM_CONFIG_MAX_WDG_MODULES          1
#define BM_CONFIG_WDG_MODULE_TIMEOUT_MS    1000
#define BM_CONFIG_WDG_MAX_NAME_LEN         16

/* Ultra 最小配置 */
#define BM_CONFIG_ULTRA_MAX_EVENT_TYPES     2
#define BM_CONFIG_ULTRA_QUEUE_DEPTH         2
#define BM_CONFIG_ULTRA_MAX_EVENT_DATA_SIZE 4

/* 缓存行 */
#define BM_CONFIG_CACHE_LINE         64u
#define BM_CONFIG_IPC_CACHE_LINE     64u

/* 使能 bm_bus（bus LATEST Δ==0 测试需要，Task 10） */
#define BM_ENABLE_BUS                1

/* 临界区屏蔽流式 IRQ（与其余 QEMU 测试保持一致） */
#define BM_HAL_CRITICAL_MASKS_STREAM_IRQ 1

#endif /* BM_CONFIG_RV64_ICOUNT_H */
