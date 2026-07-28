/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_can.h
 * @brief CAN/FDCAN HAL 接口（统一实例模型）
 *
 * 多实例设备模型：每个 `bm_hal_can` 绑定一路 CAN 控制器。
 * App 通过 vendor 配置指定引脚/AF/波特率/Message RAM/IRQ。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 CAN/FDCAN HAL 分发层
 */
#ifndef BM_HAL_CAN_H
#define BM_HAL_CAN_H

#include "drv/bm_drv_can.h"

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化 CAN 控制器
 *
 * @param dev    CAN 设备实例
 * @param config 平台相关配置指针；NULL 使用设备默认值
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；否则为平台错误码
 */
int bm_hal_can_init(const struct bm_hal_can *dev, void *config);

/**
 * @brief 启动 CAN 控制器（进入正常模式）
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；否则为平台错误码
 */
int bm_hal_can_start(const struct bm_hal_can *dev);

/**
 * @brief 停止 CAN 控制器（进入初始化模式）
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；否则为平台错误码
 */
int bm_hal_can_stop(const struct bm_hal_can *dev);

/**
 * @brief 发送一帧（异步；成功表示已提交到控制器）
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；BM_ERR_BUSY 发送队列满；
 *         BM_ERR_INVALID 参数非法
 */
int bm_hal_can_send(const struct bm_hal_can *dev, const bm_can_frame_t *frame);

/**
 * @brief 添加硬件过滤器
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；BM_ERR_INVALID 参数非法；
 *         BM_ERR_NOT_SUPPORTED 后端不支持；BM_ERR_NO_MEM 过滤器资源耗尽
 */
int bm_hal_can_add_filter(const struct bm_hal_can *dev,
                          const bm_can_filter_t *filter,
                          uint32_t *filter_id);

/**
 * @brief 移除硬件过滤器
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；BM_ERR_INVALID filter_id 非法
 */
int bm_hal_can_remove_filter(const struct bm_hal_can *dev, uint32_t filter_id);

/**
 * @brief 查询后端能力
 * @return 能力位掩码（BM_CAN_CAP_*）；无后端返回 0
 */
uint32_t bm_hal_can_get_capabilities(const struct bm_hal_can *dev);

/**
 * @brief 读取通信统计
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端；BM_ERR_INVALID stats 为 NULL
 */
int bm_hal_can_get_stats(const struct bm_hal_can *dev, bm_can_stats_t *stats);

/**
 * @brief 注册 RX 回调（NULL 取消）
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端
 */
int bm_hal_can_set_rx_callback(const struct bm_hal_can *dev,
                               bm_can_rx_callback_t cb, void *user);

/**
 * @brief 注册事件回调（NULL 取消）
 * @return BM_OK 成功；BM_ERR_NOT_INIT 无后端
 */
int bm_hal_can_set_event_callback(const struct bm_hal_can *dev,
                                  bm_can_event_callback_t cb, void *user);

#ifdef __cplusplus
}
#endif

#endif /* BM_HAL_CAN_H */
