/**
 * @file test_motor_foc_sensored.c
 * @brief 有感 FOC 组件单元测试
 *
 * 覆盖路径：
 *   - 默认路径（无 MTPA/FW）：id_ref 沿用命令层传入值，行为与改动前完全一致
 *   - MTPA 路径：enable_mtpa=1 时 id_ref 由 bm_algo_mtpa_id_ref 计算，负值验证
 *   - 弱磁路径：enable_fw=1 且上拍电压饱和时 id_ref 被进一步下调（更负）
 *   - MTPA+FW 联合路径：两者同时启用时行为正确
 *   - validate_config MTPA 参数校验：enable_mtpa=1 但 ld_h=0 返回 BM_ERR_INVALID
 */
#include "unity.h"

#include "bm/component/motor_foc_sensored.h"
#include "bm_hal_encoder_sim.h"
#include "bm_hal_pwm_sim.h"

#include <string.h>

static float s_id_feedback;
static float s_iq_feedback;
static float s_last_theta;
static bm_motor_foc_cmd_t s_callback_command;
static bm_motor_foc_telemetry_t s_callback_telemetry;
static uint32_t s_telemetry_publish_count;

void setUp(void) {
    s_id_feedback = 0.0f;
    s_iq_feedback = 0.0f;
    s_last_theta = 0.0f;
    memset(&s_callback_command, 0, sizeof(s_callback_command));
    memset(&s_callback_telemetry, 0, sizeof(s_callback_telemetry));
    s_telemetry_publish_count = 0u;
    bm_hal_pwm_request_safe_state(&BM_HAL_PWM_SIM0);
    bm_hal_encoder_sim_set_count(&BM_HAL_ENC_SIM0, 0);
    bm_hal_encoder_sim_set_fail(&BM_HAL_ENC_SIM0, 0);
}

void tearDown(void) {}

static bm_motor_foc_sensored_axis_t make_axis(void) {
    bm_motor_foc_sensored_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config = (bm_motor_foc_sensored_config_t){
        .pole_pairs = 4.0f,
        .encoder_direction = 1.0f,
        .electrical_offset_rad = 0.0f,
        .vbus_v = 24.0f,
        .phase_r_ohm = 0.5f,
        .v_max_pu = 0.95f,
        .current_dt_s = 0.0001f,
        .speed_dt_s = 0.001f,
        .iq_max_a = 2.0f,
        .pi_d = {
            .kp = 0.1f, .ki = 1.0f,
            .out_min = -0.95f, .out_max = 0.95f,
            .integrator_min = -1.0f, .integrator_max = 1.0f
        },
        .pi_q = {
            .kp = 0.1f, .ki = 1.0f,
            .out_min = -0.95f, .out_max = 0.95f,
            .integrator_min = -1.0f, .integrator_max = 1.0f
        },
        .pi_speed = {
            .kp = 0.1f, .ki = 1.0f,
            .out_min = -2.0f, .out_max = 2.0f,
            .integrator_min = -2.0f, .integrator_max = 2.0f
        },
        .speed_ramp = { .rate_per_s = 100.0f },
        .encoder = { .counts_per_rev = 4096u }
    };
    axis.resources.pwm = &BM_HAL_PWM_SIM0;
    axis.resources.encoder = &BM_HAL_ENC_SIM0;
    axis.resources.pwm_max = 1000u;
    axis.resources.current_adc_scale = 1000.0f;
    return axis;
}

/**
 * @brief 构造启用 MTPA/FW 的轴（在 make_axis 基础上叠加电机参数）。
 *
 * 典型 IPM 参数：Ld=2 mH，Lq=4 mH（凸极比 2），磁链 0.01 Wb。
 * 不连接真实 ADC；通过 sim_fb 注入 id/iq 反馈。
 */
static bm_motor_foc_sensored_axis_t make_axis_mtpa_fw(void) {
    bm_motor_foc_sensored_axis_t axis = make_axis();

    axis.config.enable_mtpa  = 1;
    axis.config.enable_fw    = 1;
    axis.config.ld_h         = 0.002f;
    axis.config.lq_h         = 0.004f;
    axis.config.psi_f_wb     = 0.01f;
    /* 注入 sim 反馈（使 current_step 跳过真实 ADC 路径）。 */
    axis.resources.sim_fb.id_a         = &s_id_feedback;
    axis.resources.sim_fb.iq_a         = &s_iq_feedback;
    return axis;
}

static void capture_voltage(void *user, float vd, float vq, float theta) {
    (void)user;
    (void)vd;
    (void)vq;
    s_last_theta = theta;
}

static int read_callback_command(void *user, bm_motor_foc_cmd_t *command) {
    (void)user;
    *command = s_callback_command;
    return 0;
}

static void capture_telemetry(
    void *user,
    const bm_motor_foc_telemetry_t *telemetry) {
    (void)user;
    s_callback_telemetry = *telemetry;
    s_telemetry_publish_count++;
}

static void test_adc_failure_latches_fault_and_stops_pwm(void) {
    bm_motor_foc_sensored_axis_t axis = make_axis();

    axis.state.cmd.status = BM_MOTOR_FOC_CMD_ENABLED;
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_pwm_enable_outputs(&BM_HAL_PWM_SIM0, 1));

    bm_motor_foc_sensored_current_step(&axis);

    TEST_ASSERT_EQUAL(1, axis.state.fault_latched);
    TEST_ASSERT_EQUAL_UINT32(1u, axis.state.fault_count);
    TEST_ASSERT_BITS(BM_MOTOR_FOC_TEL_FAULT,
                     BM_MOTOR_FOC_TEL_FAULT,
                     axis.state.telemetry.status);
    TEST_ASSERT_FALSE(bm_hal_pwm_sim_outputs_enabled(&BM_HAL_PWM_SIM0));
}

static void test_encoder_failure_latches_fault_and_stops_pwm(void) {
    bm_motor_foc_sensored_axis_t axis = make_axis();

    axis.resources.encoder = NULL;
    axis.state.cmd.status = BM_MOTOR_FOC_CMD_ENABLED;
    TEST_ASSERT_EQUAL(BM_OK,
        bm_hal_pwm_enable_outputs(&BM_HAL_PWM_SIM0, 1));

    bm_motor_foc_sensored_speed_step(&axis);

    TEST_ASSERT_EQUAL(1, axis.state.fault_latched);
    TEST_ASSERT_EQUAL_UINT32(1u, axis.state.fault_count);
    TEST_ASSERT_FALSE(bm_hal_pwm_sim_outputs_enabled(&BM_HAL_PWM_SIM0));
}

static void test_encoder_direction_and_electrical_offset_are_applied(void) {
    bm_motor_foc_sensored_axis_t axis = make_axis();

    axis.config.encoder_direction = -1.0f;
    axis.config.electrical_offset_rad = 0.25f;
    axis.resources.sim_fb.id_a = &s_id_feedback;
    axis.resources.sim_fb.iq_a = &s_iq_feedback;
    axis.resources.on_voltage = capture_voltage;
    axis.state.speed.encoder.position_rad = 0.5f;
    axis.state.cmd.status = BM_MOTOR_FOC_CMD_ENABLED;

    bm_motor_foc_sensored_current_step(&axis);

    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -1.75f, s_last_theta);
}

static void test_speed_step_preserves_current_pi_integrator(void) {
    bm_motor_foc_sensored_axis_t axis = make_axis();

    axis.state.cmd.status = BM_MOTOR_FOC_CMD_ENABLED;
    axis.state.cmd.speed_setpoint_rad_s = 5.0f;
    axis.state.current.pi_q.integrator = 0.5f;

    bm_motor_foc_sensored_speed_step(&axis);

    TEST_ASSERT_FLOAT_WITHIN(
        0.0001f, 0.5f, axis.state.current.pi_q.integrator);
}

static void test_exec_callbacks_exchange_command_and_telemetry(void) {
    bm_motor_foc_sensored_axis_t axis = make_axis();
    bm_exec_t instance;

    memset(&instance, 0, sizeof(instance));
    instance.state = &axis;
    axis.resources.sim_fb.id_a = &s_id_feedback;
    axis.resources.sim_fb.iq_a = &s_iq_feedback;
    axis.resources.read_command = read_callback_command;
    axis.resources.publish_telemetry = capture_telemetry;
    s_callback_command.sequence = 7u;
    s_callback_command.status = BM_MOTOR_FOC_CMD_ENABLED;

    bm_motor_foc_sensored_exec_current(&instance);

    TEST_ASSERT_EQUAL_UINT32(7u, axis.state.cmd.sequence);
    TEST_ASSERT_EQUAL_UINT32(1u, s_telemetry_publish_count);
    TEST_ASSERT_BITS(BM_MOTOR_FOC_TEL_VALID,
                     BM_MOTOR_FOC_TEL_VALID,
                     s_callback_telemetry.status);
}

/* ============================================================
 * MTPA / 弱磁新增测试
 * ============================================================ */

/**
 * @brief 默认路径回归：未启用 MTPA 时 id_ref 沿用 cmd.id_ref_a，不受 MTPA 影响。
 *
 * 验证：向 cmd 写入 id_ref_a=0.5A，不开 MTPA；运行一步后 PI-d 输入误差应基于
 * 0.5A（而非 MTPA 计算值），即 pi_d.integrator 因误差（0.5-0）被正向积分。
 */
static void test_default_id_ref_used_when_mtpa_disabled(void) {
    bm_motor_foc_sensored_axis_t axis = make_axis();
    /* 注入 sim 反馈，避免 ADC 不可用导致 fault。 */
    axis.resources.sim_fb.id_a = &s_id_feedback;
    axis.resources.sim_fb.iq_a = &s_iq_feedback;
    axis.state.cmd.status  = BM_MOTOR_FOC_CMD_ENABLED;
    axis.state.cmd.id_ref_a = 0.5f;
    /* 不开 MTPA，id_ref = cmd.id_ref_a = 0.5A，反馈 id=0 → 误差 0.5 > 0 */

    bm_motor_foc_sensored_current_step(&axis);

    /* PI-d 积分器正向积分，确认 id_ref=0.5 被使用（而非 MTPA 的负值）。 */
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, axis.state.current.pi_d.integrator);
}

/**
 * @brief MTPA 路径：enable_mtpa=1 时，对于 IPM 电机（Ld<Lq）id_ref 应为负值。
 *
 * bm_algo_mtpa_id_ref(iq>0, Ld<Lq, psi_f>0) 返回负值（d 轴去磁电流）；
 * cmd.id_ref_a 故意设为 0，MTPA 覆盖后 id_ref<0，PI-d 输入误差 <0，
 * 积分器被负向驱动。
 */
static void test_mtpa_sets_negative_id_ref_for_ipm_motor(void) {
    bm_motor_foc_sensored_axis_t axis = make_axis_mtpa_fw();

    axis.config.enable_fw   = 0; /* 本用例只验证 MTPA，关闭 FW。 */
    axis.state.cmd.status   = BM_MOTOR_FOC_CMD_ENABLED;
    axis.state.cmd.id_ref_a = 0.0f;
    /* iq_ref 来自 speed 环，直接注入到 state。 */
    axis.state.speed.iq_ref_a = 1.0f; /* 1A q 轴电流参考 */
    /* 反馈 id=0，MTPA 算出 id_ref<0 → 误差 = id_ref-0 < 0 → 积分器负向。 */

    bm_motor_foc_sensored_current_step(&axis);

    TEST_ASSERT_LESS_THAN_FLOAT(0.0f, axis.state.current.pi_d.integrator);
}

/**
 * @brief 弱磁路径：enable_fw=1 且上拍电压已饱和时，id_ref 被进一步下调（更负）。
 *
 * 先注入一个接近 v_max_pu 的上拍电压（模拟饱和），再执行 current_step；
 * FW 应在 MTPA 基础上进一步降低 id_ref，使 PI-d 积分器被更深负向驱动。
 */
static void test_fw_further_decreases_id_ref_when_voltage_saturated(void) {
    bm_motor_foc_sensored_axis_t axis_mtpa_only = make_axis_mtpa_fw();
    bm_motor_foc_sensored_axis_t axis_mtpa_fw   = make_axis_mtpa_fw();
    float integrator_mtpa_only;
    float integrator_mtpa_fw;

    /* 两轴均注入相同初始条件。
     * 弱磁触发条件：v_mag = sqrt(vd²+vq²) > v_max_pu；
     * 同时非零 vd/vq 使矢量幅值超出限幅圆。 */
    axis_mtpa_only.config.enable_fw = 0;
    axis_mtpa_only.state.cmd.status   = BM_MOTOR_FOC_CMD_ENABLED;
    axis_mtpa_only.state.speed.iq_ref_a = 1.0f;
    axis_mtpa_only.state.current.last_vd_pu = 0.5f * axis_mtpa_only.config.v_max_pu;
    axis_mtpa_only.state.current.last_vq_pu = 0.9f * axis_mtpa_only.config.v_max_pu;
    /* 合成 v_mag ≈ sqrt(0.5²+0.9²)*v_max ≈ 1.03*v_max > v_max → 弱磁触发。 */

    axis_mtpa_fw.state.cmd.status   = BM_MOTOR_FOC_CMD_ENABLED;
    axis_mtpa_fw.state.speed.iq_ref_a = 1.0f;
    axis_mtpa_fw.state.current.last_vd_pu = 0.5f * axis_mtpa_fw.config.v_max_pu;
    axis_mtpa_fw.state.current.last_vq_pu = 0.9f * axis_mtpa_fw.config.v_max_pu;

    bm_motor_foc_sensored_current_step(&axis_mtpa_only);
    bm_motor_foc_sensored_current_step(&axis_mtpa_fw);

    integrator_mtpa_only = axis_mtpa_only.state.current.pi_d.integrator;
    integrator_mtpa_fw   = axis_mtpa_fw.state.current.pi_d.integrator;

    /* 开 FW 的轴 id_ref 更负 → 误差更负 → 积分器比仅 MTPA 更小（更负）。 */
    TEST_ASSERT_LESS_THAN_FLOAT(integrator_mtpa_only, integrator_mtpa_fw);
}

/**
 * @brief MTPA + FW 联合路径：两者同时启用时 current_step 正常完成（无 fault）。
 */
static void test_mtpa_and_fw_combined_no_fault(void) {
    bm_motor_foc_sensored_axis_t axis = make_axis_mtpa_fw();

    axis.state.cmd.status     = BM_MOTOR_FOC_CMD_ENABLED;
    axis.state.speed.iq_ref_a = 1.0f;
    axis.state.current.last_vd_pu = 0.0f;
    axis.state.current.last_vq_pu = axis.config.v_max_pu;

    bm_motor_foc_sensored_current_step(&axis);

    TEST_ASSERT_EQUAL(0, axis.state.fault_latched);
    TEST_ASSERT_BITS(BM_MOTOR_FOC_TEL_VALID,
                     BM_MOTOR_FOC_TEL_VALID,
                     axis.state.telemetry.status);
}

/**
 * @brief validate_config MTPA 参数校验：enable_mtpa=1 但 ld_h=0 → BM_ERR_INVALID。
 *
 * 与 sensorless validate_config 校验逻辑完全对称。
 */
static void test_validate_config_rejects_mtpa_without_inductance(void) {
    bm_motor_foc_sensored_config_t cfg;
    bm_motor_foc_sensored_axis_t axis = make_axis();

    cfg = axis.config;
    cfg.enable_mtpa = 1;
    cfg.ld_h = 0.0f;    /* 非法：ld_h 须 > 0 */
    cfg.lq_h = 0.004f;
    cfg.psi_f_wb = 0.01f;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_motor_foc_sensored_validate_config(&cfg));
}

/**
 * @brief validate_config 正常路径：enable_mtpa=1 且参数合法时返回 BM_OK。
 */
static void test_validate_config_accepts_valid_mtpa_params(void) {
    bm_motor_foc_sensored_config_t cfg;
    bm_motor_foc_sensored_axis_t axis = make_axis();

    cfg = axis.config;
    cfg.enable_mtpa = 1;
    cfg.ld_h    = 0.002f;
    cfg.lq_h    = 0.004f;
    cfg.psi_f_wb = 0.01f;

    TEST_ASSERT_EQUAL(BM_OK,
                      bm_motor_foc_sensored_validate_config(&cfg));
}

/**
 * @brief encoder_timeout_s>0：瞬时丢样（累计 < 超时）速度环容忍、不 latch。
 */
static void test_encoder_timeout_tolerates_transient_dropout(void) {
    bm_motor_foc_sensored_axis_t axis = make_axis();
    axis.config.encoder_timeout_s = 0.05f;          /* 容忍 50 拍 @1ms */
    axis.state.cmd.status = BM_MOTOR_FOC_CMD_ENABLED;
    bm_hal_encoder_sim_set_fail(&BM_HAL_ENC_SIM0, 1);

    for (int i = 0; i < 10; ++i) {                  /* 0.01s < 0.05s */
        bm_motor_foc_sensored_speed_step(&axis);
    }

    TEST_ASSERT_EQUAL(0, axis.state.fault_latched);
}

/**
 * @brief encoder_timeout_s>0：持续丢样累计超阈 → latch_fault 进安全态。
 */
static void test_encoder_timeout_latches_after_sustained_dropout(void) {
    bm_motor_foc_sensored_axis_t axis = make_axis();
    axis.config.encoder_timeout_s = 0.05f;          /* 50 拍 @1ms */
    axis.state.cmd.status = BM_MOTOR_FOC_CMD_ENABLED;
    bm_hal_encoder_sim_set_fail(&BM_HAL_ENC_SIM0, 1);

    for (int i = 0; i < 60; ++i) {                  /* 0.06s > 0.05s */
        bm_motor_foc_sensored_speed_step(&axis);
    }

    TEST_ASSERT_EQUAL(1, axis.state.fault_latched);
}

/**
 * @brief 读成功清零累计：丢样未超阈→恢复一拍→再丢样仍从零计，不被历史误 latch。
 */
static void test_encoder_timeout_resets_on_successful_read(void) {
    bm_motor_foc_sensored_axis_t axis = make_axis();
    axis.config.encoder_timeout_s = 0.05f;
    axis.state.cmd.status = BM_MOTOR_FOC_CMD_ENABLED;

    bm_hal_encoder_sim_set_fail(&BM_HAL_ENC_SIM0, 1);
    for (int i = 0; i < 40; ++i) {                  /* 0.04s < 0.05s */
        bm_motor_foc_sensored_speed_step(&axis);
    }
    TEST_ASSERT_EQUAL(0, axis.state.fault_latched);

    bm_hal_encoder_sim_set_fail(&BM_HAL_ENC_SIM0, 0);  /* 恢复一拍清零 */
    bm_motor_foc_sensored_speed_step(&axis);

    bm_hal_encoder_sim_set_fail(&BM_HAL_ENC_SIM0, 1);
    for (int i = 0; i < 40; ++i) {                  /* 再 0.04s，从零计 → 不 latch */
        bm_motor_foc_sensored_speed_step(&axis);
    }
    TEST_ASSERT_EQUAL(0, axis.state.fault_latched);
}

/**
 * @brief last_vd/vq_pu 在 current_step 后被更新（供下一拍弱磁使用）。
 */
static void test_last_vd_vq_updated_after_current_step(void) {
    bm_motor_foc_sensored_axis_t axis = make_axis();

    axis.resources.sim_fb.id_a = &s_id_feedback;
    axis.resources.sim_fb.iq_a = &s_iq_feedback;
    axis.state.cmd.status     = BM_MOTOR_FOC_CMD_ENABLED;
    axis.state.speed.iq_ref_a = 1.0f;
    /* 初始上拍电压为 0。 */
    axis.state.current.last_vd_pu = 0.0f;
    axis.state.current.last_vq_pu = 0.0f;

    bm_motor_foc_sensored_current_step(&axis);

    /* 至少有一个方向的电压被更新（非零，因为 iq_ref != 0）。 */
    TEST_ASSERT_NOT_EQUAL(0.0f, axis.state.current.last_vq_pu);
}

int main(void) {
    UNITY_BEGIN();
    /* 默认路径回归（改动前行为不变）。 */
    RUN_TEST(test_adc_failure_latches_fault_and_stops_pwm);
    RUN_TEST(test_encoder_failure_latches_fault_and_stops_pwm);
    RUN_TEST(test_encoder_direction_and_electrical_offset_are_applied);
    RUN_TEST(test_speed_step_preserves_current_pi_integrator);
    RUN_TEST(test_encoder_timeout_tolerates_transient_dropout);
    RUN_TEST(test_encoder_timeout_latches_after_sustained_dropout);
    RUN_TEST(test_encoder_timeout_resets_on_successful_read);
    RUN_TEST(test_exec_callbacks_exchange_command_and_telemetry);
    /* MTPA / 弱磁新增路径。 */
    RUN_TEST(test_default_id_ref_used_when_mtpa_disabled);
    RUN_TEST(test_mtpa_sets_negative_id_ref_for_ipm_motor);
    RUN_TEST(test_fw_further_decreases_id_ref_when_voltage_saturated);
    RUN_TEST(test_mtpa_and_fw_combined_no_fault);
    RUN_TEST(test_validate_config_rejects_mtpa_without_inductance);
    RUN_TEST(test_validate_config_accepts_valid_mtpa_params);
    RUN_TEST(test_last_vd_vq_updated_after_current_step);
    return UNITY_END();
}
