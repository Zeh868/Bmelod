/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file stepper_pulse.h
 * @brief STEP/DIR 脉冲步进驱动组件
 *
 * 组件实现 STEP/DIR/EN 脉冲调度；公开配置、静态轴实例与服务操作接口由
 * @ref bm_stepper_pulse_service.h 提供，使平台适配器仅依赖 common 服务契约。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1 步进伺服栈）
 * 2026-07-28       1.1            zeh            dir_hold/min 脉宽/GPIO fault/en_set
 * 2026-07-28       1.2            zeh            set_enable 注明 fault 态允许断使能
 * 2026-07-28       1.3            zeh            类型与服务接口下沉至 common 契约
 */
#ifndef BM_STEPPER_PULSE_H
#define BM_STEPPER_PULSE_H

#include "bm/common/bm_stepper_pulse_service.h"

#endif /* BM_STEPPER_PULSE_H */
