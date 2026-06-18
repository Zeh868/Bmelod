/**
 * @file bm_hal_comp.h
 * @brief 比较器 HAL 接口
 *
 * 清除比较器锁存状态，用于过流等保护信号的复位。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 *
 */
#ifndef BM_HAL_COMP_H
#define BM_HAL_COMP_H

#include "drv/bm_drv_comp.h"
#include "bm/common/bm_types.h"

typedef struct bm_hal_comp bm_hal_comp_t;

/**
 * @brief 清除比较器锁存状态
 *
 * @param comp 比较器实例指针
 * @return BM_OK 成功；否则为平台错误码
 */
int bm_hal_comp_clear_latch(const bm_hal_comp_t *comp);

#endif /* BM_HAL_COMP_H */
