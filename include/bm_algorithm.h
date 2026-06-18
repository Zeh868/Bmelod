/**
 * @file bm_algorithm.h
 * @brief 算法库聚合头：按配置引入各算法族公共 API
 *
 * 算法库不依赖 bm_core，仅依赖 bm_config 与 C 标准库（部分模块使用 libm）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            正式发布
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#ifndef BM_ALGORITHM_H
#define BM_ALGORITHM_H

#include "bm/algorithm/bm_algo_common.h"
#include "bm/algorithm/bm_algo_control.h"
#include "bm/algorithm/bm_algo_filter.h"
#include "bm/algorithm/bm_algo_profile.h"
#include "bm/algorithm/bm_algo_motion.h"
#include "bm/algorithm/bm_algo_motor.h"
#include "bm/algorithm/bm_algo_power.h"
#include "bm/algorithm/bm_algo_battery.h"
#include "bm/algorithm/bm_algo_fft.h"
#include "bm/algorithm/bm_algo_spectral.h"
#include "bm/algorithm/bm_algo_resample.h"
#include "bm/algorithm/bm_algo_fusion.h"
#include "bm/algorithm/bm_algo_audio.h"
#include "bm/algorithm/bm_algo_image.h"
#include "bm/algorithm/bm_algo_features.h"
#include "bm/algorithm/bm_algo_calibration.h"
#include "bm/algorithm/bm_algo_statistics.h"
#include "bm/algorithm/bm_algo_comm.h"
#include "bm/algorithm/bm_algo_signal_quality.h"
#include "bm/algorithm/bm_algo_estimator.h"
#include "bm/algorithm/bm_algo_fixed.h"
#include "bm/algorithm/bm_algo_vision.h"
#include "bm/algorithm/bm_algo_detection.h"
#include "bm/algorithm/bm_algo_compensation.h"
#include "bm/algorithm/bm_algo_battery_model.h"
#include "bm/algorithm/bm_algo_power_quality.h"

#endif /* BM_ALGORITHM_H */
