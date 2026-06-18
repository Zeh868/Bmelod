/**
 * @file bm_sync.h
 * @brief 多控制实例硬件同步域
 *
 * 通过主定时器与相位偏移，协调多个控制实例的 PWM/采样时序。
 *
 * @core_affinity 同核
 * 同步域的所有成员实例须在同一 CPU 上。configure/arm/trigger
 * 须由成员所在 CPU 调用。
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
#ifndef BM_SYNC_H
#define BM_SYNC_H

#include "bm/hybrid/bm_exec.h"
#include "bm/common/bm_types.h"

typedef struct bm_hal_timer bm_hal_timer_t;

/** 同步域配置 */
typedef struct {
    const char *name;
    const bm_hal_timer_t *master_timer;
    const bm_exec_t *const *members;
    const uint32_t *phase_ticks;
    uint32_t member_count;
} bm_sync_domain_t;

/** 同步域框架层状态（单活动域，见 bm_runtime_model.h） */
typedef enum {
    BM_SYNC_STATE_IDLE = 0,
    BM_SYNC_STATE_CONFIGURED,
    BM_SYNC_STATE_ARMED,
    BM_SYNC_STATE_TRANSITION
} bm_sync_state_t;

/** 同步 API 仅限主上下文调用，不可在 ISR 中调用。 */

/**
 * @brief 配置同步域并委托 HAL 完成硬件设置
 *
 * @param domain 同步域描述符指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；其他为 HAL 错误码
 */
int bm_sync_configure(const bm_sync_domain_t *domain);

/**
 * @brief 武装同步域，准备触发
 *
 * @param domain 同步域描述符指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；其他为 HAL 错误码
 */
int bm_sync_arm(const bm_sync_domain_t *domain);

/**
 * @brief 触发同步域，按相位启动各成员
 *
 * @param domain 同步域描述符指针
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；其他为 HAL 错误码
 */
int bm_sync_trigger(const bm_sync_domain_t *domain);

/**
 * @brief 安全停止同步域并复位 HAL 状态
 *
 * 若已有活动域，始终停止内部记录的活动域。
 *
 * @param domain 同步域描述符指针，可为 NULL
 */
void bm_sync_safe_stop(const bm_sync_domain_t *domain);

/**
 * @brief 查询同步域框架层状态（只读，临界区内快照）
 *
 * HAL 配置、武装、触发或安全停止尚未完成时返回
 * BM_SYNC_STATE_TRANSITION。
 */
bm_sync_state_t bm_sync_get_state(void);

#endif /* BM_SYNC_H */
