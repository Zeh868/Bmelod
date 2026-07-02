/**
 * @file test_motor_foc_sensorless.c
 * @brief motor_foc_sensorless 组件单元测试
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 */
#include "unity.h"
#include "bm/component/motor_foc_sensorless.h"

#include <math.h>
#include <string.h>

static float g_sim_id;
static float g_sim_iq;
static float g_sim_theta;

static int read_cmd(void *user, bm_motor_sl_cmd_t *command) {
    (void)user;
    command->sequence = 1u;
    command->status = BM_MOTOR_SL_CMD_ENABLED;
    command->iq_ref_a = 1.0f;
    return 0;
}

static void publish_tel(void *user, const bm_motor_sl_telemetry_t *telemetry) {
    (void)user;
    (void)telemetry;
}

static void init_axis(bm_motor_foc_sensorless_axis_t *axis) {
    memset(axis, 0, sizeof(*axis));
    axis->config.pole_pairs = 4.0f;
    axis->config.vbus_v = 24.0f;
    axis->config.phase_r_ohm = 0.5f;
    axis->config.ld_h = 0.001f;
    axis->config.lq_h = 0.002f;
    axis->config.psi_f_wb = 0.05f;
    axis->config.v_max_pu = 0.95f;
    axis->config.current_dt_s = 0.0001f;
    axis->config.iq_max_a = 5.0f;
    axis->config.enable_mtpa = 1;
    axis->config.enable_fw = 1;
    axis->config.pi_d = (bm_algo_pi_config_t){
        .kp = 0.5f, .ki = 50.0f, .out_min = -1.0f, .out_max = 1.0f,
        .integrator_min = -2.0f, .integrator_max = 2.0f
    };
    axis->config.pi_q = axis->config.pi_d;
    axis->config.observer = (bm_algo_flux_observer_config_t){
        .rs_ohm = 0.5f, .ls_h = 0.001f, .pll_kp = 20.0f, .pll_ki = 200.0f
    };
    axis->config.align_time_s = 0.001f;
    axis->config.align_id_a = 0.2f;
    axis->config.open_loop_omega_start = 50.0f;
    axis->config.open_loop_omega_end = 100.0f;
    axis->config.open_loop_ramp_s = 0.01f;
    axis->config.observer_lock_omega_rad_s = 1.0f;
    axis->config.observer_lock_time_s = 10.0f;
    axis->resources.sim_fb.id_a = &g_sim_id;
    axis->resources.sim_fb.iq_a = &g_sim_iq;
    axis->resources.sim_fb.theta_elec_rad = &g_sim_theta;
    axis->resources.read_command = read_cmd;
    axis->resources.publish_telemetry = publish_tel;
}

void setUp(void) {
    g_sim_id = 0.0f;
    g_sim_iq = 0.0f;
    g_sim_theta = 0.0f;
}

void tearDown(void) {
}

void test_motor_foc_sensorless_runs_current_loop(void) {
    bm_motor_foc_sensorless_axis_t axis;
    uint32_t i;

    init_axis(&axis);
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_motor_foc_sensorless_validate_config(&axis.config));
    bm_motor_foc_sensorless_reset(&axis);
    axis.state.cmd.status = BM_MOTOR_SL_CMD_ENABLED;
    axis.state.cmd.iq_ref_a = 1.0f;

    for (i = 0u; i < 200u; ++i) {
        g_sim_theta += 0.01f;
        g_sim_iq += 0.001f;
        bm_motor_foc_sensorless_current_step(&axis);
    }

    TEST_ASSERT_TRUE(axis.state.loop_count >= 200u);
    TEST_ASSERT_TRUE(fabsf(axis.state.telemetry.iq_ref_a) > 0.5f);
    TEST_ASSERT_TRUE(axis.state.fault_latched == 0);
    TEST_ASSERT_EQUAL(BM_MOTOR_SL_PHASE_OBSERVER, axis.state.phase);
}

void test_motor_foc_sensorless_phase_transitions(void) {
    bm_motor_foc_sensorless_axis_t axis;
    uint32_t i;
    int saw_align = 0;
    int saw_open = 0;
    int saw_observer = 0;

    init_axis(&axis);
    bm_motor_foc_sensorless_reset(&axis);
    bm_motor_foc_sensorless_apply_command(&axis, &(bm_motor_sl_cmd_t){
        .status = BM_MOTOR_SL_CMD_ENABLED,
        .iq_ref_a = 1.0f
    });

    for (i = 0u; i < 300u; ++i) {
        g_sim_theta += 0.05f;
        bm_motor_foc_sensorless_current_step(&axis);
        if (axis.state.phase == BM_MOTOR_SL_PHASE_ALIGN) {
            saw_align = 1;
        }
        if (axis.state.phase == BM_MOTOR_SL_PHASE_OPEN_LOOP) {
            saw_open = 1;
        }
        if (axis.state.phase == BM_MOTOR_SL_PHASE_OBSERVER) {
            saw_observer = 1;
            break;
        }
    }

    TEST_ASSERT_TRUE(saw_align != 0);
    TEST_ASSERT_TRUE(saw_open != 0);
    TEST_ASSERT_TRUE(saw_observer != 0);
}

void test_motor_foc_sensorless_sim_uses_feedback_theta(void) {
    bm_motor_foc_sensorless_axis_t axis;

    init_axis(&axis);
    bm_motor_foc_sensorless_reset(&axis);
    axis.state.cmd.status = BM_MOTOR_SL_CMD_ENABLED;
    axis.state.cmd.iq_ref_a = 1.0f;
    axis.state.phase = BM_MOTOR_SL_PHASE_OBSERVER;

    g_sim_theta = 1.23f;
    g_sim_id = 0.1f;
    g_sim_iq = 0.2f;
    bm_motor_foc_sensorless_current_step(&axis);

    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.23f,
                             axis.state.telemetry.theta_elec_rad);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.1f, axis.state.telemetry.id_meas_a);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.2f, axis.state.telemetry.iq_meas_a);
}

void test_motor_foc_sensorless_rejects_invalid_observer(void) {
    bm_motor_foc_sensorless_axis_t axis;

    init_axis(&axis);
    axis.config.observer.ls_h = 0.0f;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_motor_foc_sensorless_validate_config(&axis.config));
}

/*
 * ==========================================================================
 * Phase 1 Task 2：FOC 无感真 ADC 注入桩测试
 *
 * 参见 docs/superpowers/plans/2026-07-02-arch-improvement-schedule.md。
 * 以上测试均走 resources.sim_fb（旁路 ADC，直接注入 id/iq）；本组不设
 * sim_fb，而是注入假 ADC 驱动（struct bm_adc_driver_api），验证 P0-5a
 * 修复后 ALIGN/OPEN_LOOP 相电流环确实闭合在真实 ADC 采样路径上。
 * ==========================================================================
 */

/** @brief 假 ADC 驱动累计调用次数（read_injected 被调用的总次数） */
static int s_fake_adc_read_calls;
/** @brief 假 ADC A 相注入原始码（16 位无符号，32768 为零电流中点） */
static uint16_t s_fake_adc_raw_ia;
/** @brief 假 ADC B 相注入原始码（16 位无符号，32768 为零电流中点） */
static uint16_t s_fake_adc_raw_ib;
/** @brief 假 ADC 是否模拟读取失败（非零：read_injected 返回错误码） */
static int s_fake_adc_fail;

/**
 * @brief 假 ADC 注入通道读取回调（静态辅助）
 *
 * rank 0 返回 A 相注入码，rank 1 返回 B 相注入码；s_fake_adc_fail 非零
 * 时模拟硬件读取失败，用于驱动 latch_fault 路径。
 */
static int fake_adc_read_injected(const struct bm_hal_adc *dev,
                                  uint32_t rank, uint16_t *value) {
    (void)dev;
    s_fake_adc_read_calls++;
    if (s_fake_adc_fail) {
        return BM_ERR_TIMEOUT;
    }
    *value = (rank == 0u) ? s_fake_adc_raw_ia : s_fake_adc_raw_ib;
    return BM_OK;
}

/** @brief 假 ADC 驱动 API 表（仅实现 read_injected，bind_complete 不使用） */
static const struct bm_adc_driver_api s_fake_adc_api = {
    fake_adc_read_injected,
    NULL
};

/** @brief 假 ADC HAL 句柄，绑定 s_fake_adc_api */
static const struct bm_hal_adc s_fake_adc = {
    &s_fake_adc_api,
    NULL
};

/**
 * @brief 构造走真实 ADC 路径（不旁路 sim_fb）的 axis（静态辅助）
 *
 * config 与 init_axis() 一致，但 resources 绑定假 ADC 而非 sim_fb；
 * align_time_s 放宽到 0.05s，保证测试步数内始终停在 ALIGN 相。
 */
static void init_axis_real_adc(bm_motor_foc_sensorless_axis_t *axis) {
    memset(axis, 0, sizeof(*axis));
    axis->config.pole_pairs = 4.0f;
    axis->config.vbus_v = 24.0f;
    axis->config.phase_r_ohm = 0.5f;
    axis->config.ld_h = 0.001f;
    axis->config.lq_h = 0.002f;
    axis->config.psi_f_wb = 0.05f;
    axis->config.v_max_pu = 0.95f;
    axis->config.current_dt_s = 0.0001f;
    axis->config.iq_max_a = 5.0f;
    axis->config.enable_mtpa = 1;
    axis->config.enable_fw = 1;
    axis->config.pi_d = (bm_algo_pi_config_t){
        .kp = 0.5f, .ki = 50.0f, .out_min = -1.0f, .out_max = 1.0f,
        .integrator_min = -2.0f, .integrator_max = 2.0f
    };
    axis->config.pi_q = axis->config.pi_d;
    axis->config.observer = (bm_algo_flux_observer_config_t){
        .rs_ohm = 0.5f, .ls_h = 0.001f, .pll_kp = 20.0f, .pll_ki = 200.0f
    };
    axis->config.align_time_s = 0.05f;
    axis->config.align_id_a = 0.2f;
    axis->config.open_loop_omega_start = 50.0f;
    axis->config.open_loop_omega_end = 100.0f;
    axis->config.open_loop_ramp_s = 0.01f;
    axis->config.observer_lock_omega_rad_s = 1.0f;
    axis->config.observer_lock_time_s = 10.0f;
    /* 不设置 resources.sim_fb（保持全 NULL），走真实 ADC 采样路径。 */
    axis->resources.adc = &s_fake_adc;
    axis->resources.adc_rank_ia = 0u;
    axis->resources.adc_rank_ib = 1u;
    axis->resources.current_adc_scale = 6000.0f;
    axis->resources.read_command = NULL;
    axis->resources.publish_telemetry = NULL;
}

/**
 * @brief P0-5a 回归：ALIGN 相电流环闭合在真实 ADC 采样值上
 *
 * 注入一组非零 A/B 相 ADC 码运行数步，记录测得 d 轴电流；再切换为另一
 * 组明显不同（符号相反）的 ADC 码继续运行，测得电流应随之显著变化，
 * 证明电流环确实读取了真实 ADC（而非停留在旧 bug 下的恒零反馈）。
 */
void test_foc_sl_align_reads_real_adc(void) {
    bm_motor_foc_sensorless_axis_t axis;
    float id_meas_1;
    float id_meas_2;
    uint32_t i;

    init_axis_real_adc(&axis);
    s_fake_adc_read_calls = 0;
    s_fake_adc_fail = 0;

    TEST_ASSERT_EQUAL(BM_OK,
                      bm_motor_foc_sensorless_validate_config(&axis.config));
    bm_motor_foc_sensorless_reset(&axis);
    axis.state.cmd.status = BM_MOTOR_SL_CMD_ENABLED;
    axis.state.cmd.iq_ref_a = 0.0f;

    s_fake_adc_raw_ia = (uint16_t)(BM_ADC_MIDPOINT_16BIT + 6000);
    s_fake_adc_raw_ib = (uint16_t)(BM_ADC_MIDPOINT_16BIT - 4000);
    for (i = 0u; i < 5u; ++i) {
        bm_motor_foc_sensorless_current_step(&axis);
    }
    TEST_ASSERT_EQUAL(BM_MOTOR_SL_PHASE_ALIGN, axis.state.phase);
    TEST_ASSERT_TRUE(axis.state.fault_latched == 0);
    id_meas_1 = axis.state.telemetry.id_meas_a;

    s_fake_adc_raw_ia = (uint16_t)(BM_ADC_MIDPOINT_16BIT - 6000);
    s_fake_adc_raw_ib = (uint16_t)(BM_ADC_MIDPOINT_16BIT + 4000);
    for (i = 0u; i < 5u; ++i) {
        bm_motor_foc_sensorless_current_step(&axis);
    }
    TEST_ASSERT_EQUAL(BM_MOTOR_SL_PHASE_ALIGN, axis.state.phase);
    TEST_ASSERT_TRUE(axis.state.fault_latched == 0);
    id_meas_2 = axis.state.telemetry.id_meas_a;

    /* 桩确被调用：10 步 × 每步 A/B 两路注入采样。 */
    TEST_ASSERT_EQUAL_INT(20, s_fake_adc_read_calls);
    /* 电流环输出随注入电流显著变化（反向翻转注入码应带来明显差异）。 */
    TEST_ASSERT_TRUE(fabsf(id_meas_1 - id_meas_2) > 0.5f);
}

/**
 * @brief P0-5a 回归：真实 ADC 读取失败时锁存故障并输出安全值
 */
void test_foc_sl_align_adc_fail_latches_fault(void) {
    bm_motor_foc_sensorless_axis_t axis;

    init_axis_real_adc(&axis);
    s_fake_adc_read_calls = 0;
    s_fake_adc_fail = 1;

    TEST_ASSERT_EQUAL(BM_OK,
                      bm_motor_foc_sensorless_validate_config(&axis.config));
    bm_motor_foc_sensorless_reset(&axis);
    axis.state.cmd.status = BM_MOTOR_SL_CMD_ENABLED;
    axis.state.cmd.iq_ref_a = 0.0f;

    bm_motor_foc_sensorless_current_step(&axis);

    TEST_ASSERT_TRUE(s_fake_adc_read_calls >= 1);
    TEST_ASSERT_TRUE(axis.state.fault_latched != 0);
    TEST_ASSERT_EQUAL(BM_MOTOR_SL_PHASE_FAULT, axis.state.phase);
    TEST_ASSERT_EQUAL(BM_MOTOR_SL_PHASE_FAULT, axis.state.telemetry.phase);
    TEST_ASSERT_TRUE((axis.state.telemetry.status & BM_MOTOR_SL_TEL_FAULT) != 0u);
    /* 安全值：PI 已在 latch_fault 内复位，最近一帧电压指令保持为 0。 */
    TEST_ASSERT_EQUAL_FLOAT(0.0f, axis.state.last_vd_pu);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, axis.state.last_vq_pu);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_motor_foc_sensorless_runs_current_loop);
    RUN_TEST(test_motor_foc_sensorless_sim_uses_feedback_theta);
    RUN_TEST(test_motor_foc_sensorless_phase_transitions);
    RUN_TEST(test_motor_foc_sensorless_rejects_invalid_observer);
    RUN_TEST(test_foc_sl_align_reads_real_adc);
    RUN_TEST(test_foc_sl_align_adc_fail_latches_fault);
    return UNITY_END();
}
