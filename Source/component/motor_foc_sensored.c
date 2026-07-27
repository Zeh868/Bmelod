/**
 * @file motor_foc_sensored.c
 * @brief 有感 FOC 伺服轴领域组件实现
 * @author zeh (china_qzh@163.com)
 * @version 0.7
 * @date 2026-07-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       0.1            zeh            初始骨架
 * 2026-06-22       0.2            zeh            B3 诊断：current_step 读 ADC 后回填 ia_raw/ib_raw 到遥测
 * 2026-06-23       0.3            zeh            MTPA/弱磁：电流环前按 config 开关接入
 *                                                bm_algo_mtpa_id_ref / bm_algo_fw_id_adjust；
 *                                                validate_config 新增 MTPA 参数校验；
 *                                                current_step 更新 last_vd/vq_pu 供弱磁使用
 * 2026-06-23       0.4            zeh            encoder 丢样容忍：opt-in encoder_timeout_s
 *                                                （默认 0=旧行为），speed_step 超时才 latch
 * 2026-06-24       0.5            zeh            speed_step 加 opt-in speed_feedback_sign
 *                                                （<0 翻 speed_meas，修镜像轴正反馈跑飞）
 * 2026-07-09       0.6            zeh            validate_config 补 iq_max_a>0 校验（与
 *                                                sensorless 对齐）；current_step 禁用分支
 *                                                补发遥测（清 VALID/SAT/FAULT 位），修陈旧 status
 * 2026-07-13       0.7            zeh            C9：read_theta_elec 改由编码器单圈原始
 *                                                计数求电角度（有界量、精度不随圈数
 *                                                劣化），替代无界 position_rad×pole_pairs
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "bm/component/motor_foc_sensored.h"
#include "bm/algorithm/bm_algo_common.h"
#include "bm/algorithm/bm_algo_motor.h"
#include "bm/common/bm_types.h"
#include "bm/component/bm_component_common.h"

#include <math.h>
#include <string.h>

#ifndef BM_ALGO_PI_F
#define BM_ALGO_PI_F 3.14159265358979323846f
#endif

static float read_theta_elec(const bm_motor_foc_sensored_axis_t *axis) {
    const bm_motor_foc_sensored_config_t *cfg = &axis->config;
    const bm_motor_foc_sensored_resources_t *res = &axis->resources;
    float mech_single_turn_rad;

    if (res->sim_fb.theta_elec_rad != NULL) {
        return bm_algo_angle_wrap_rad(*res->sim_fb.theta_elec_rad);
    }
    /* C9：电角度改由编码器单圈原始计数（encoder.prev_count，硬件回绕于
     * counts_per_rev，恒落在 [0,cpr)，与 encoder_update 跨圈检测的既有
     * 契约一致）直接求得，量值有界、float 精度恒满。原式用无界累计的
     * position_rad × pole_pairs：turns×cpr 超 2^24 后 float 分辨率低于
     * 编码器分辨率且随圈数线性劣化，连续旋转数分钟即出现可测电角度误差。
     * 数学等价前提：pole_pairs 为整数（电机物理必然）——此时两式相差
     * turns×2π×pole_pairs×direction 为 2π 整数倍，wrap 后一致。 */
    mech_single_turn_rad =
        (float)axis->state.speed.encoder.prev_count *
        (2.0f * BM_ALGO_PI_F / (float)cfg->encoder.counts_per_rev);
    return bm_algo_angle_wrap_rad(
        cfg->encoder_direction *
        mech_single_turn_rad * cfg->pole_pairs +
        cfg->electrical_offset_rad);
}

/**
 * @brief 读取双相电流 ADC 并转换为实际电流（A）。
 *
 * B3 诊断扩展：新增 raw_ia_out / raw_ib_out 可选参数，非 NULL 时回传 Clarke
 * 变换前的原始 ADC 计数（uint16，0~65535）；NULL 则忽略（保持原有调用兼容）。
 *
 * @param[in]  axis       轴句柄。
 * @param[out] ia         换算后 ia（A）。
 * @param[out] ib         换算后 ib（A）。
 * @param[out] raw_ia_out B3 诊断：原始 ia ADC 计数（可为 NULL）。
 * @param[out] raw_ib_out B3 诊断：原始 ib ADC 计数（可为 NULL）。
 * @return BM_OK 成功；BM_ERR_INVALID 参数/资源配置非法；BM_ERR_IO HAL 读取失败。
 */
static int read_current_ab(const bm_motor_foc_sensored_axis_t *axis,
                           float *ia,
                           float *ib,
                           uint16_t *raw_ia_out,
                           uint16_t *raw_ib_out) {
    const bm_motor_foc_sensored_resources_t *res = &axis->resources;
    uint16_t raw_ia = 0u;
    uint16_t raw_ib = 0u;

    if (res->adc == NULL || res->current_adc_scale <= 0.0f ||
        ia == NULL || ib == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_hal_adc_read_injected(res->adc, res->adc_rank_ia, &raw_ia) != BM_OK) {
        return BM_ERR_IO;
    }
    if (bm_hal_adc_read_injected(res->adc, res->adc_rank_ib, &raw_ib) != BM_OK) {
        return BM_ERR_IO;
    }
    *ia = bm_component_adc_to_current(res->current_adc_scale, raw_ia);
    *ib = bm_component_adc_to_current(res->current_adc_scale, raw_ib);
    /* B3 诊断：可选回传原始计数（NULL 则跳过）。 */
    if (raw_ia_out != NULL) { *raw_ia_out = raw_ia; }
    if (raw_ib_out != NULL) { *raw_ib_out = raw_ib; }
    return BM_OK;
}

/**
 * @brief 读取 dq 反馈电流（支持 sim_fb 注入路径 + 真实 ADC 路径）。
 *
 * B3 诊断扩展：新增 raw_ia_out / raw_ib_out 可选参数，非 NULL 时在真实 ADC
 * 路径里把 Clarke 变换前的原始计数透传给调用方；sim_fb 注入路径不填（置 0）。
 *
 * @param[in]  axis       轴句柄。
 * @param[in]  theta_elec 本拍电角度（rad）。
 * @param[out] id         d 轴实测电流（A）。
 * @param[out] iq         q 轴实测电流（A）。
 * @param[out] raw_ia_out B3 诊断：原始 ia ADC 计数（可为 NULL）。
 * @param[out] raw_ib_out B3 诊断：原始 ib ADC 计数（可为 NULL）。
 * @return BM_OK 成功；read_current_ab 的错误码透传。
 */
static int read_id_iq_feedback(const bm_motor_foc_sensored_axis_t *axis,
                               float theta_elec,
                               float *id,
                               float *iq,
                               uint16_t *raw_ia_out,
                               uint16_t *raw_ib_out) {
    const bm_motor_foc_sensored_resources_t *res = &axis->resources;
    bm_algo_alphabeta_t i_ab;
    bm_algo_dq_t      i_dq;

    if (res->sim_fb.id_a != NULL && res->sim_fb.iq_a != NULL) {
        *id = *res->sim_fb.id_a;
        *iq = *res->sim_fb.iq_a;
        /* sim_fb 路径无真实 ADC，raw 置 0。 */
        if (raw_ia_out != NULL) { *raw_ia_out = 0u; }
        if (raw_ib_out != NULL) { *raw_ib_out = 0u; }
        return BM_OK;
    }

    {
        float ia;
        float ib;
        int rc;

        rc = read_current_ab(axis, &ia, &ib, raw_ia_out, raw_ib_out);
        if (rc != BM_OK) {
            return rc;
        }
        bm_algo_clarke_2shunt(ia, ib, &i_ab);
        bm_algo_park(&i_ab, theta_elec, &i_dq);
        *id = i_dq.id;
        *iq = i_dq.iq;
    }
    return BM_OK;
}

static void latch_fault(bm_motor_foc_sensored_axis_t *axis) {
    bm_motor_foc_sensored_state_t *st = &axis->state;

    if (!st->fault_latched) {
        st->fault_latched = 1;
        st->fault_count++;
    }
    st->speed.iq_ref_a = 0.0f;
    bm_algo_pi_reset(&st->current.pi_d, 0.0f);
    bm_algo_pi_reset(&st->current.pi_q, 0.0f);
    bm_algo_pi_reset(&st->speed.pi_speed, 0.0f);
    if (axis->resources.pwm != NULL) {
        bm_hal_pwm_request_safe_state(axis->resources.pwm);
    }
}

static void set_fault_telemetry(bm_motor_foc_sensored_state_t *st) {
    bm_motor_foc_telemetry_t *tel = &st->telemetry;

    tel->sequence = st->current.loop_count;
    tel->status = BM_MOTOR_FOC_TEL_FAULT;
    tel->iq_ref_a = 0.0f;
}

static void sync_command(bm_motor_foc_sensored_axis_t *axis) {
    bm_motor_foc_cmd_t command;

    if (axis->resources.read_command != NULL &&
        axis->resources.read_command(
            axis->resources.read_command_user, &command) == 0) {
        bm_motor_foc_sensored_apply_command(axis, &command);
    }
}

static void publish_telemetry(const bm_motor_foc_sensored_axis_t *axis) {
    BM_COMPONENT_PUBLISH_TELEMETRY(axis, &axis->state.telemetry);
}

int bm_motor_foc_sensored_validate_config(
    const bm_motor_foc_sensored_config_t *config) {
    if (config == NULL) {
        return BM_ERR_INVALID;
    }
    /* Batch-3：pole_pairs 须为正整数（C9 电角度公式数学前提），
     * 拒绝 3.5 类误配。 */
    if (!bm_algo_is_finite_f(config->pole_pairs) || config->pole_pairs <= 0.0f ||
        floorf(config->pole_pairs) != config->pole_pairs ||
        (config->encoder_direction != 1.0f &&
         config->encoder_direction != -1.0f) ||
        config->vbus_v <= 0.0f ||
        config->phase_r_ohm <= 0.0f) {
        return BM_ERR_INVALID;
    }
    if (config->current_dt_s <= 0.0f || config->speed_dt_s <= 0.0f) {
        return BM_ERR_INVALID;
    }
    if (config->encoder.counts_per_rev == 0u) {
        return BM_ERR_INVALID;
    }
    /* 缺口 9：sensored 版此前漏了 iq_max_a>0 校验，与 sensorless 版对齐补齐。 */
    if (config->iq_max_a <= 0.0f) {
        return BM_ERR_INVALID;
    }
    /* MTPA 启用时，电机电感/磁链参数须有效（与 sensorless 校验逻辑对齐）。 */
    if (config->enable_mtpa &&
        (config->ld_h <= 0.0f || config->lq_h <= 0.0f)) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

void bm_motor_foc_sensored_reset(bm_motor_foc_sensored_axis_t *axis) {
    const bm_motor_foc_sensored_config_t *cfg;
    bm_motor_foc_cmd_t saved_cmd;

    if (axis == NULL) {
        return;
    }
    cfg = &axis->config;
    saved_cmd = axis->state.cmd;
    memset(&axis->state, 0, sizeof(axis->state));
    axis->state.cmd = saved_cmd;
    bm_algo_pi_reset(&axis->state.current.pi_d, 0.0f);
    bm_algo_pi_reset(&axis->state.current.pi_q, 0.0f);
    bm_algo_pi_reset(&axis->state.speed.pi_speed, 0.0f);
    bm_algo_ramp_reset(&axis->state.speed.speed_ramp, 0.0f);
    /* 编码器机械原点取 0：当前 encoder 配置无零点偏移字段，机械零点由安装
     * 标定决定；电角度偏移由 electrical_offset_rad 在 read_theta_elec 中单独补偿。 */
    bm_algo_encoder_reset(&axis->state.speed.encoder, &cfg->encoder, 0);
}

void bm_motor_foc_sensored_apply_command(bm_motor_foc_sensored_axis_t *axis,
                                        const bm_motor_foc_cmd_t *cmd) {
    if (axis == NULL || cmd == NULL) {
        return;
    }
    axis->state.cmd = *cmd;
}

void bm_motor_foc_sensored_current_step(bm_motor_foc_sensored_axis_t *axis) {
    bm_motor_foc_sensored_config_t *cfg;
    bm_motor_foc_sensored_resources_t *res;
    bm_motor_foc_sensored_state_t *st;
    bm_motor_foc_telemetry_t *tel;
    float theta_elec;
    float id_meas;
    float iq_meas;
    float vd;
    float vq;
    bm_algo_dq_t v_dq;
    bm_algo_alphabeta_t v_ab;
    bm_algo_svpwm_out_t svpwm;
    int saturated;

    if (axis == NULL) {
        return;
    }
    cfg = &axis->config;
    res = &axis->resources;
    st = &axis->state;
    tel = &st->telemetry;

    if ((st->cmd.status & BM_MOTOR_FOC_CMD_FAULT) != 0u ||
        st->fault_latched) {
        latch_fault(axis);
        set_fault_telemetry(st);
        st->current.loop_count++;
        return;
    }

    if ((st->cmd.status & BM_MOTOR_FOC_CMD_ENABLED) == 0u) {
        st->speed.iq_ref_a = 0.0f;
        bm_algo_pi_reset(&st->current.pi_d, 0.0f);
        bm_algo_pi_reset(&st->current.pi_q, 0.0f);
        bm_algo_pi_reset(&st->speed.pi_speed, 0.0f);
        if (res->pwm != NULL) {
            (void)bm_hal_pwm_set_duty(res->pwm, 0u, 0u);
            (void)bm_hal_pwm_set_duty(res->pwm, 1u, 0u);
            (void)bm_hal_pwm_set_duty(res->pwm, 2u, 0u);
        }
        /* 缺口 14：禁用分支此前不更新遥测，导致发布出去的 status 仍是上一拍
         * （可能是 VALID）的陈旧值。此处清空 VALID/SAT/FAULT 位并刷新
         * sequence/iq_ref_a，使遥测如实反映当前禁用/安全态。 */
        tel->sequence = st->current.loop_count;
        tel->status = 0u;
        tel->iq_ref_a = 0.0f;
        st->current.loop_count++;
        return;
    }

    theta_elec = read_theta_elec(axis);
    {
        /* B3 诊断：顺路采集原始 ADC 计数（Clarke 变换前），回填到遥测供应用层录入黑匣子。
         * 对控制路径无任何新增开销：raw_ia/ib 只是把 read_current_ab 内已有的局部变量
         * 透传出来，不增加任何额外 HAL 调用或浮点运算。 */
        uint16_t raw_ia = 0u;
        uint16_t raw_ib = 0u;
        if (read_id_iq_feedback(axis, theta_elec, &id_meas, &iq_meas,
                                &raw_ia, &raw_ib) != BM_OK) {
            latch_fault(axis);
            set_fault_telemetry(st);
            st->current.loop_count++;
            return;
        }
        /* 回填原始计数到遥测（始终写，开关在应用层 publish_telemetry 处管控）。 */
        tel->ia_raw = raw_ia;
        tel->ib_raw = raw_ib;
    }

    {
        /* ---------- id_ref / iq_ref 分配（MTPA + 弱磁，与 sensorless 逻辑对齐）---------- */
        float id_ref;
        float iq_ref;

        iq_ref = st->speed.iq_ref_a;

        /* MTPA：按 iq_ref 计算最优 id_ref，降低铜损；未启用时沿用命令层传入值。 */
        if (cfg->enable_mtpa) {
            id_ref = bm_algo_mtpa_id_ref(iq_ref, cfg->ld_h, cfg->lq_h,
                                         cfg->psi_f_wb);
        } else {
            id_ref = st->cmd.id_ref_a;
        }

        /* 弱磁：电压饱和时下调 id_ref，扩展高速转速范围；未启用时不调整。 */
        if (cfg->enable_fw) {
            id_ref = bm_algo_fw_id_adjust(id_ref,
                                          st->current.last_vd_pu,
                                          st->current.last_vq_pu,
                                          cfg->v_max_pu);
        }

        vd = bm_algo_pi_step(&st->current.pi_d, &cfg->pi_d,
                             id_ref - id_meas, cfg->current_dt_s);
        {
            float vq_ff = cfg->phase_r_ohm * iq_ref / cfg->vbus_v;
            float vq_pi = bm_algo_pi_step(&st->current.pi_q, &cfg->pi_q,
                                          iq_ref - iq_meas,
                                          cfg->current_dt_s);
            vq = vq_ff + vq_pi;
        }
    }
    bm_algo_voltage_limit(&vd, &vq, cfg->v_max_pu);
    /* 更新上一拍电压，供下一拍弱磁算法读取。 */
    st->current.last_vd_pu = vd;
    st->current.last_vq_pu = vq;
    saturated = (fabsf(vd) >= cfg->v_max_pu - 1e-4f) ||
                (fabsf(vq) >= cfg->v_max_pu - 1e-4f);

    v_dq.id = vd;
    v_dq.iq = vq;
    bm_algo_inv_park(&v_dq, theta_elec, &v_ab);
    bm_algo_svpwm(v_ab.i_alpha * 0.5f * cfg->vbus_v,
                  v_ab.i_beta * 0.5f * cfg->vbus_v,
                  cfg->vbus_v,
                  &svpwm);

    if (res->pwm != NULL && res->pwm_max > 0u) {
        (void)bm_hal_pwm_set_duty(res->pwm, 0u,
            (uint16_t)(svpwm.duty_a * (float)res->pwm_max));
        (void)bm_hal_pwm_set_duty(res->pwm, 1u,
            (uint16_t)(svpwm.duty_b * (float)res->pwm_max));
        (void)bm_hal_pwm_set_duty(res->pwm, 2u,
            (uint16_t)(svpwm.duty_c * (float)res->pwm_max));
    }

    if (res->on_voltage != NULL) {
        res->on_voltage(res->on_voltage_user, vd, vq, theta_elec);
    }

    tel->sequence = st->current.loop_count;
    tel->status = BM_MOTOR_FOC_TEL_VALID;
    if (saturated) {
        tel->status |= BM_MOTOR_FOC_TEL_SAT;
    }
    tel->id_meas_a = id_meas;
    tel->iq_meas_a = iq_meas;
    tel->speed_rad_s = st->speed.encoder.velocity_rad_s;
    tel->theta_elec_rad = theta_elec;
    tel->iq_ref_a = st->speed.iq_ref_a;
    st->current.loop_count++;
}

void bm_motor_foc_sensored_speed_step(bm_motor_foc_sensored_axis_t *axis) {
    bm_motor_foc_sensored_config_t *cfg;
    bm_motor_foc_sensored_resources_t *res;
    bm_motor_foc_sensored_state_t *st;
    int32_t enc_raw = 0;
    float speed_ref;
    float speed_meas;
    float iq_ref;

    if (axis == NULL) {
        return;
    }
    cfg = &axis->config;
    res = &axis->resources;
    st = &axis->state;

    if (st->cmd.sequence != st->speed.last_cmd_sequence) {
        bm_algo_pi_reset(&st->speed.pi_speed, 0.0f);
        bm_algo_ramp_reset(&st->speed.speed_ramp,
                           st->speed.encoder.velocity_rad_s);
        st->speed.last_cmd_sequence = st->cmd.sequence;
    }

    if ((st->cmd.status & BM_MOTOR_FOC_CMD_ENABLED) == 0u ||
        (st->cmd.status & BM_MOTOR_FOC_CMD_FAULT) != 0u ||
        st->fault_latched) {
        st->speed.iq_ref_a = 0.0f;
        return;
    }

    if (res->encoder == NULL ||
        bm_hal_encoder_read(res->encoder, &enc_raw) != BM_OK) {
        /* 读失败：encoder_timeout_s<=0 维持旧行为（立即 latch）；
         * >0 则短时容忍——保持上次有效速度，累计连续丢样，超阈才 latch。 */
        if (cfg->encoder_timeout_s <= 0.0f) {
            latch_fault(axis);
            return;
        }
        st->speed.encoder_lost_time_s += cfg->speed_dt_s;
        if (st->speed.encoder_lost_time_s >= cfg->encoder_timeout_s) {
            latch_fault(axis);
            return;
        }
        speed_meas = st->speed.last_velocity_rad_s;  /* 丢样窗内保持上次速度 */
    } else {
        st->speed.encoder_lost_time_s = 0.0f;        /* 读成功清零累计 */
        (void)bm_algo_encoder_update(&st->speed.encoder, &cfg->encoder,
                                     enc_raw, cfg->speed_dt_s);
        speed_meas = st->speed.encoder.velocity_rad_s;
        st->speed.last_velocity_rad_s = speed_meas;  /* 记录供丢样保持 */
    }
    /* per-axis 速度反馈符号修正：镜像安装的轴 encoder 原始计数方向与转矩约定相反，
     * 致速度环正反馈跑飞。speed_feedback_sign<0 时取反；0/+1 不翻（向后兼容）。 */
    if (cfg->speed_feedback_sign < 0.0f) {
        speed_meas = -speed_meas;
    }
    speed_ref = bm_algo_ramp_step(&st->speed.speed_ramp, &cfg->speed_ramp,
                                  st->cmd.speed_setpoint_rad_s,
                                  cfg->speed_dt_s);
    iq_ref = bm_algo_pi_step(&st->speed.pi_speed, &cfg->pi_speed,
                             speed_ref - speed_meas, cfg->speed_dt_s);
    iq_ref = bm_algo_clamp_f(iq_ref, -cfg->iq_max_a, cfg->iq_max_a);
    st->speed.iq_ref_a = iq_ref;
}

void bm_motor_foc_sensored_exec_current(const bm_exec_t *instance) {
    if (instance == NULL || instance->state == NULL) {
        return;
    }
    bm_motor_foc_sensored_axis_t *axis =
        (bm_motor_foc_sensored_axis_t *)instance->state;

    sync_command(axis);
    bm_motor_foc_sensored_current_step(axis);
    publish_telemetry(axis);
}

void bm_motor_foc_sensored_exec_speed(const bm_exec_t *instance) {
    if (instance == NULL || instance->state == NULL) {
        return;
    }
    bm_motor_foc_sensored_axis_t *axis =
        (bm_motor_foc_sensored_axis_t *)instance->state;

    sync_command(axis);
    bm_motor_foc_sensored_speed_step(axis);
}

int bm_motor_foc_sensored_exec_init(const bm_exec_t *instance) {
    bm_motor_foc_sensored_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return BM_ERR_INVALID;
    }
    axis = (bm_motor_foc_sensored_axis_t *)instance->state;
    if (bm_motor_foc_sensored_validate_config(&axis->config) != BM_OK) {
        return BM_ERR_INVALID;
    }
    bm_motor_foc_sensored_reset(axis);
    return BM_OK;
}

int bm_motor_foc_sensored_exec_start(const bm_exec_t *instance) {
    const bm_motor_foc_sensored_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return BM_ERR_INVALID;
    }
    axis = (const bm_motor_foc_sensored_axis_t *)instance->state;
    if (axis->resources.pwm == NULL) {
        return BM_OK;
    }
    return bm_hal_pwm_enable_outputs(axis->resources.pwm, 1);
}

void bm_motor_foc_sensored_exec_safe_stop(const bm_exec_t *instance) {
    const bm_motor_foc_sensored_axis_t *axis;

    if (instance == NULL || instance->state == NULL) {
        return;
    }
    axis = (const bm_motor_foc_sensored_axis_t *)instance->state;
    if (axis->resources.pwm != NULL) {
        bm_hal_pwm_request_safe_state(axis->resources.pwm);
    }
}

const bm_exec_ops_t bm_motor_foc_sensored_exec_ops = {
    bm_motor_foc_sensored_exec_init,
    bm_motor_foc_sensored_exec_start,
    bm_motor_foc_sensored_exec_safe_stop
};
