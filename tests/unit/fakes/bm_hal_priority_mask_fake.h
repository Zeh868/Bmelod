/**
 * @file bm_hal_priority_mask_fake.h
 * @brief 优先级掩码 HAL 桩：供单元测试查询与重置模拟 BASEPRI 状态
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 * @par 修改日志:
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 */

#ifndef BM_HAL_PRIORITY_MASK_FAKE_H
#define BM_HAL_PRIORITY_MASK_FAKE_H

#include <stdint.h>

/** 读取桩当前 BASEPRI 值 */
uint8_t bm_hal_priority_mask_fake_basepri(void);
/** 重置桩 BASEPRI 与 PRIMASK 为初始状态 */
void bm_hal_priority_mask_fake_reset(void);

#endif /* BM_HAL_PRIORITY_MASK_FAKE_H */
