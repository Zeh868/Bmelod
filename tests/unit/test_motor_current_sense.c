/**
 * @file test_motor_current_sense.c
 * @brief motor_current_sense 单元测试
 *
 * 覆盖 PWM 扇区采样窗口判定、ADC 读取失败路径、sim_fb 路径
 * 及 validate_config 字段校验。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-09
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-17       1.0            zeh            正式发布
 * 2026-06-23       1.1            zeh            补 ADC 失败/sim_fb/validate 字段校验测试
 * 2026-07-09       1.2            zeh            补缺口 16 回归：3-shunt 真实 ADC ic
 *                                                与仿真 ia/ib 混用破坏 KCL
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "unity.h"
#include "bm/component/motor_current_sense.h"
#include "bm/common/bm_types.h"
#include "hal/bm_hal_adc.h"
#include "drv/bm_drv_adc.h"

#include <math.h>
#include <string.h>

static float g_ia;
static float g_ib;

/**
 * @brief 缺口 16：假 ADC，用于构造真实硬件与 sim_fb 同时挂载的 HIL 场景，
 * 验证 3-shunt 分支是否会把真实采到的 ic 与仿真注入的 ia/ib 混用。
 */
static int s_fake_adc_ic_calls;
static uint16_t s_fake_adc_raw_ic;

static int fake_adc_read_injected(const struct bm_hal_adc *dev,
                                  uint32_t rank, uint16_t *value) {
    (void)dev;
    (void)rank;
    s_fake_adc_ic_calls++;
    *value = s_fake_adc_raw_ic;
    return BM_OK;
}

static const struct bm_adc_driver_api s_fake_adc_api = {
    fake_adc_read_injected,
    NULL
};

static const struct bm_hal_adc s_fake_adc = {
    &s_fake_adc_api,
    NULL
};

void setUp(void) {
    g_ia = 1.0f;
    g_ib = -0.5f;
    s_fake_adc_ic_calls = 0;
    s_fake_adc_raw_ic = 0u;
}

void tearDown(void) {}

void test_motor_current_sense_sample_window(void) {
    bm_motor_current_sense_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config.topology = BM_MOTOR_CS_2SHUNT;
    axis.config.pwm_sector = 0u;
    axis.config.adc_phase_deg = 30.0f;
    axis.config.sample_window_deg = 20.0f;
    axis.resources.sim_fb.ia_a = &g_ia;
    axis.resources.sim_fb.ib_a = &g_ib;

    TEST_ASSERT_EQUAL(BM_OK, bm_motor_current_sense_init(&axis));
    TEST_ASSERT_EQUAL(BM_OK, bm_motor_current_sense_step(&axis));
    TEST_ASSERT_EQUAL(1, axis.state.sample_valid);
    TEST_ASSERT_EQUAL(1, axis.state.valid);

    axis.config.adc_phase_deg = 200.0f;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_motor_current_sense_step(&axis));
    TEST_ASSERT_EQUAL(0, axis.state.sample_valid);
}

/* ADC 为 NULL 时 init 应失败（无 sim_fb） */
void test_motor_current_sense_init_no_adc_no_simfb(void) {
    bm_motor_current_sense_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config.topology = BM_MOTOR_CS_2SHUNT;
    axis.config.adc_phase_deg = 0.0f;
    axis.config.sample_window_deg = 0.0f;
    /* resources 全零：adc=NULL，sim_fb 全 NULL */

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_motor_current_sense_init(&axis));
}

/* sim_fb 路径：直接注入浮点电流，step 应成功且 valid=1 */
void test_motor_current_sense_simfb_path(void) {
    bm_motor_current_sense_axis_t axis;

    memset(&axis, 0, sizeof(axis));
    axis.config.topology = BM_MOTOR_CS_2SHUNT;
    axis.config.adc_phase_deg = 0.0f;
    axis.config.sample_window_deg = 0.0f;
    axis.resources.sim_fb.ia_a = &g_ia;
    axis.resources.sim_fb.ib_a = &g_ib;

    TEST_ASSERT_EQUAL(BM_OK, bm_motor_current_sense_init(&axis));
    TEST_ASSERT_EQUAL(BM_OK, bm_motor_current_sense_step(&axis));
    TEST_ASSERT_EQUAL(1, axis.state.valid);
    /* 2-shunt Clarke：alphabeta.i_alpha ≈ ia = 1.0 */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, axis.state.alphabeta.i_alpha);
}

/* validate_config 字段校验：topology 非法 */
void test_motor_current_sense_validate_bad_topology(void) {
    bm_motor_current_sense_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.topology = (bm_motor_current_sense_topology_t)99;
    cfg.adc_phase_deg = 0.0f;
    cfg.sample_window_deg = 0.0f;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_motor_current_sense_validate_config(&cfg));
}

/* validate_config 字段校验：adc_phase_deg 越界 */
void test_motor_current_sense_validate_bad_phase(void) {
    bm_motor_current_sense_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.topology = BM_MOTOR_CS_2SHUNT;
    cfg.adc_phase_deg = 360.0f; /* 须 < 360 */
    cfg.sample_window_deg = 0.0f;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_motor_current_sense_validate_config(&cfg));
}

/* validate_config 字段校验：sample_window_deg 越界 */
void test_motor_current_sense_validate_bad_window(void) {
    bm_motor_current_sense_config_t cfg;

    memset(&cfg, 0, sizeof(cfg));
    cfg.topology = BM_MOTOR_CS_2SHUNT;
    cfg.adc_phase_deg = 30.0f;
    cfg.sample_window_deg = 180.0f; /* 须 < 180 */

    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_motor_current_sense_validate_config(&cfg));
}

/**
 * @brief 缺口 16 回归：3-shunt 拓扑下，若同时挂了真实 ADC 资源（HIL 场景）且仅
 * 注入 sim_fb.ia_a/ib_a（不注入 ic_a），此前会用真实 ADC 采到的 ic 与仿真的
 * ia/ib 混算，破坏 ia+ib+ic=0 的物理约束。
 */
void test_motor_current_sense_3shunt_sim_ia_ib_mixed_with_real_adc_ic_breaks_kcl(void) {
    bm_motor_current_sense_axis_t axis;
    float sum;

    memset(&axis, 0, sizeof(axis));
    axis.config.topology = BM_MOTOR_CS_3SHUNT;
    axis.config.adc_phase_deg = 0.0f;
    axis.config.sample_window_deg = 0.0f;
    /* 仅注入 ia/ib（ic_a=NULL），同时挂着真实 ADC 资源。 */
    axis.resources.sim_fb.ia_a = &g_ia;   /* 1.0f */
    axis.resources.sim_fb.ib_a = &g_ib;   /* -0.5f */
    axis.resources.adc = &s_fake_adc;
    axis.resources.rank_ic = 2u;
    axis.resources.adc_scale = 1000.0f;

    /* 真实 ADC 码与 ia/ib 完全无关（明显偏离满足 KCL 所需的码）。 */
    s_fake_adc_raw_ic = (uint16_t)(BM_ADC_MIDPOINT_16BIT + 20000);

    TEST_ASSERT_EQUAL(BM_OK, bm_motor_current_sense_init(&axis));
    TEST_ASSERT_EQUAL(BM_OK, bm_motor_current_sense_step(&axis));

    sum = axis.state.abc.ia + axis.state.abc.ib + axis.state.abc.ic;
    /* 应满足 ia+ib+ic≈0（ic 由 -(ia+ib) 算出，与仿真基准一致），而不是被
     * 真实 ADC 采到的无关 ic 拖到 20A 量级。 */
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, sum);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_motor_current_sense_sample_window);
    RUN_TEST(test_motor_current_sense_init_no_adc_no_simfb);
    RUN_TEST(test_motor_current_sense_simfb_path);
    RUN_TEST(test_motor_current_sense_validate_bad_topology);
    RUN_TEST(test_motor_current_sense_validate_bad_phase);
    RUN_TEST(test_motor_current_sense_validate_bad_window);
    RUN_TEST(test_motor_current_sense_3shunt_sim_ia_ib_mixed_with_real_adc_ic_breaks_kcl);
    return UNITY_END();
}
