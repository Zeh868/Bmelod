/**
 * @file bms_demo_shared.h
 * @brief BMS Demo 共用库仑计量参数（hrt_bms_coulomb / stream_bms_pipeline 对齐）
 *
 * HRT 路径：ADC 快照 → 周期积分（bm_algo_coulomb_step）。
 * 流式路径：块电流 → LPF → bm_pipeline 库仑节点。
 * 二者共用本头中的容量与效率契约，SOC 语义一致。
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
 */
#ifndef BMS_DEMO_SHARED_H
#define BMS_DEMO_SHARED_H

#include "bm/algorithm/bm_algo_battery.h"

/** 参考 Pack 标称容量（Ah） */
#define BMS_DEMO_NOMINAL_CAPACITY_AH    1.0f
/** 库仑效率 */
#define BMS_DEMO_COULOMB_EFFICIENCY       1.0f
/** 充电电流参考（A），HRT/流式 Demo 验收用 */
#define BMS_DEMO_CHARGE_CURRENT_A         20.0f
/** SOC 初值 */
#define BMS_DEMO_SOC_INIT                 0.50f

static inline void bms_demo_coulomb_config_init(bm_algo_coulomb_config_t *cfg) {
    if (cfg == NULL) {
        return;
    }
    cfg->nominal_capacity_ah = BMS_DEMO_NOMINAL_CAPACITY_AH;
    cfg->coulomb_efficiency = BMS_DEMO_COULOMB_EFFICIENCY;
    cfg->soc_min = 0.0f;
    cfg->soc_max = 1.0f;
}

#endif /* BMS_DEMO_SHARED_H */
