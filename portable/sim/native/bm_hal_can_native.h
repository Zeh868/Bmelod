/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_can_native.h
 * @brief native_sim CAN/FDCAN 后端测试辅助接口
 * @maturity E1
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 native_sim CAN 后端测试辅助
 * 2026-07-28       1.1            zeh            新增 bm_hal_can_native_rx_frame
 *                                             （读取 RX 缓冲队列）
 *
 * @par ????:
 * 2026-08-01       1.1            Codex            补全中文 Doxygen 合规注释
 */
#ifndef BM_HAL_CAN_NATIVE_H
#define BM_HAL_CAN_NATIVE_H

#include "drv/bm_drv_can.h"
#include "hal/bm_hal_can.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief native_sim 默认控制台 CAN 设备（实例 0，send 打印到 stdout） */
extern const struct bm_hal_can bm_can_default;

/** @brief native_sim CAN 实例 1（loopback 测试用） */
extern const struct bm_hal_can bm_native_can1;

/** @brief 重置所有 native CAN 实例状态 */
void bm_hal_can_native_reset(void);

/**
 * @brief 向指定实例注入一帧 RX 帧
 *
 * 如果已注册 rx_callback，直接调用回调；否则暂存到内部 RX 队列，
 * 供测试通过 bm_hal_can_native_rx_frame 读取。
 */
void bm_hal_can_native_inject_rx(const struct bm_hal_can *dev,
                                 const bm_can_frame_t *frame);

/**
 * @brief 向指定实例注入事件（BM_CAN_EVT_* 组合）
 */
void bm_hal_can_native_inject_event(const struct bm_hal_can *dev,
                                    uint32_t event);

/**
 * @brief 触发 bus-off 事件（BM_CAN_EVT_BUS_OFF）
 */
void bm_hal_can_native_trigger_bus_off(const struct bm_hal_can *dev);

/**
 * @brief 触发 bus-off 恢复事件（BM_CAN_EVT_BUS_OFF_RECOVER）
 */
void bm_hal_can_native_recover_bus_off(const struct bm_hal_can *dev);

/**
 * @brief 读取实例 1 已发送帧数量
 */
size_t bm_hal_can_native_tx_count(const struct bm_hal_can *dev);

/**
 * @brief 读取实例最近一次发送的帧
 * @return BM_OK 成功；BM_ERR_INVALID 无发送帧
 */
int bm_hal_can_native_tx_frame(const struct bm_hal_can *dev,
                               bm_can_frame_t *frame);

/**
 * @brief 读取实例 RX 缓冲队列中最早的一帧（读后移除，rx_count 递减）
 * @return BM_OK 成功；BM_ERR_INVALID 无缓冲帧或参数非法
 */
int bm_hal_can_native_rx_frame(const struct bm_hal_can *dev,
                               bm_can_frame_t *frame);

/**
 * @brief 读取实例当前过滤器数量
 */
size_t bm_hal_can_native_filter_count(const struct bm_hal_can *dev);

#ifdef __cplusplus
}
#endif

#endif /* BM_HAL_CAN_NATIVE_H */
