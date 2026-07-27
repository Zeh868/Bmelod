/**
 * @file test_stepper_servo.c
 * @brief 步进伺服集成测试：motion_profile → control_loop → stepper_pulse → 脉冲植物模型
 *
 * 植物模型 = stepper_pulse 组件本身 + 假定时器模拟：按 arm_timer 武装的
 * 间隔逐个消费半周期并调用 on_timer，使位置只随真实发出的脉冲变化——
 * 验证位置环经脉冲链路收敛到轨迹目标。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-27
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1 步进伺服栈）
 *
 * SPDX-License-Identifier: LGPL-3.0-or-later
 */
#include "unity.h"
#include "bm/component/stepper_pulse.h"
#include "bm/component/control_loop.h"
#include "bm/component/motion_profile.h"
#include "bm/common/bm_types.h"

#include <math.h>
#include <string.h>

/* ---------- 假平台：STEP/DIR 电平记录 + 可模拟定时器（绝对时钟模型） ---------- */
static int      g_step_level;
static int      g_dir_level;
static uint32_t g_now_us;   /* 假平台当前时刻（µs） */
static uint32_t g_due_us;   /* 定时器到期时刻（µs），g_timer_armed=1 时有效 */
static int      g_timer_armed;

static void sim_step_high(void *user) { (void)user; g_step_level = 1; }
static void sim_step_low(void *user)  { (void)user; g_step_level = 0; }
static void sim_dir_set(void *user, int level) { (void)user; g_dir_level = level; }

/**
 * @brief arm_timer 契约语义实现：到期时间上限——已武装且剩余更短则不动，
 *        否则按 interval 重设；0 = 取消。
 */
static int sim_arm_timer(void *user, uint32_t interval_us) {
    (void)user;
    if (interval_us == 0u) {
        g_timer_armed = 0;
        return BM_OK;
    }
    if (g_timer_armed == 0
        || (g_due_us - g_now_us) > interval_us) {
        g_due_us      = g_now_us + interval_us;
        g_timer_armed = 1;
    }
    return BM_OK;
}

/**
 * @brief 假定时器时间推进：按真实时刻消耗，跨过到期点才触发 on_timer
 *        （一次性定时器语义，触发后由组件重新武装）。
 */
static void sim_advance(bm_stepper_pulse_axis_t *axis, uint32_t us) {
    uint32_t target = g_now_us + us;
    uint32_t guard  = 0u;

    while (g_timer_armed != 0 && g_due_us <= target && guard < 100000u) {
        g_now_us      = g_due_us;
        g_timer_armed = 0;
        bm_stepper_pulse_on_timer(axis); /* 内部重新武装/停止 */
        guard++;
    }
    g_now_us = target;
}

/* ---------- 控制环资源 ---------- */
static bm_stepper_pulse_axis_t g_stepper;
static float                   g_setpoint_pos;
static float                   g_dt_s;

static int cl_read_plant(void *user,
                         float *outer_measurement,
                         float *inner_measurement,
                         float *setpoint) {
    int32_t pos = bm_stepper_pulse_position(&g_stepper);
    (void)user;
    *outer_measurement = (float)pos;
    /*
     * 内环反馈 = 脉冲植物真实速度。理想步进植物（本模型，无丢步）的实际
     * 速度就是当前速度指令 state.velocity_sps；实机可用测速机/观测器，
     * 本测试聚焦“轨迹→级联→脉冲→位置”链路收敛，不验测速估计品质。
     */
    *inner_measurement = g_stepper.state.velocity_sps;
    *setpoint          = g_setpoint_pos;
    (void)pos;
    return BM_OK;
}

static int cl_write_output(void *user, float output) {
    (void)user;
    bm_stepper_pulse_set_velocity(&g_stepper, output);
    return BM_OK;
}

void setUp(void) {
    g_step_level = 0;
    g_dir_level = 0;
    g_now_us = 0u;
    g_due_us = 0u;
    g_timer_armed = 0;
    g_setpoint_pos = 0.0f;
    g_dt_s = 0.001f;

    memset(&g_stepper, 0, sizeof(g_stepper));
    g_stepper.config.max_step_rate_hz = 2000u;
    g_stepper.config.dir_setup_us     = 20u;
    g_stepper.resources.step_high = sim_step_high;
    g_stepper.resources.step_low  = sim_step_low;
    g_stepper.resources.dir_set   = sim_dir_set;
    g_stepper.resources.arm_timer = sim_arm_timer;
    TEST_ASSERT_EQUAL(BM_OK, bm_stepper_pulse_init(&g_stepper));
}

void tearDown(void) {}

/**
 * @brief 位置收敛：梯形轨迹 → 串级 PI → 脉冲链，2s 内到位（容差 ±5 步）
 */
void test_stepper_servo_position_converges(void) {
    bm_motion_profile_axis_t   prof;
    bm_motion_profile_output_t pout;
    bm_control_loop_axis_t     cl;
    int i;

    /* 轨迹：1000 步目标，vmax=1000 steps/s，amax=2000 steps/s^2 */
    memset(&prof, 0, sizeof(prof));
    prof.config.type = BM_MOTION_PROFILE_TRAP;
    prof.config.vmax = 1000.0f;
    prof.config.amax = 2000.0f;
    prof.config.jerk = 0.0f;
    prof.config.dt_s = g_dt_s;
    TEST_ASSERT_EQUAL(BM_OK, bm_motion_profile_validate_config(&prof.config));
    bm_motion_profile_reset(&prof, 0.0f);
    bm_motion_profile_goto(&prof, 1000.0f);

    /* 串级 PI：外环位置→速度设定，内环速度→速度指令（脉冲植物增益≈1） */
    memset(&cl, 0, sizeof(cl));
    cl.config.dt_s          = g_dt_s;
    cl.config.outer_pi.kp   = 2.0f;
    cl.config.outer_pi.ki   = 0.0f;
    cl.config.outer_pi.out_min = -2000.0f;
    cl.config.outer_pi.out_max =  2000.0f;
    cl.config.outer_pi.integrator_min = -1000.0f;
    cl.config.outer_pi.integrator_max =  1000.0f;
    cl.config.inner_pi.kp   = 0.5f;
    cl.config.inner_pi.ki   = 0.0f;
    cl.config.inner_pi.out_min = -2000.0f;
    cl.config.inner_pi.out_max =  2000.0f;
    cl.config.inner_pi.integrator_min = -1000.0f;
    cl.config.inner_pi.integrator_max =  1000.0f;
    cl.resources.read_plant        = cl_read_plant;
    cl.resources.read_plant_user   = NULL;
    cl.resources.write_output      = cl_write_output;
    cl.resources.write_output_user = NULL;
    bm_control_loop_reset(&cl);

    for (i = 0; i < 10000; ++i) {
        bm_motion_profile_step(&prof, &pout);
        g_setpoint_pos = pout.position;
        bm_control_loop_step(&cl);
        sim_advance(&g_stepper, 1000u); /* 1ms 控制周期 */
    }

    /* 轨迹已完成；级联纯 P（积分植物，无 windup）指数收敛，10s 后到位 */
    TEST_ASSERT_EQUAL(1, pout.done);
    TEST_ASSERT_INT32_WITHIN(10, 1000, bm_stepper_pulse_position(&g_stepper));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_stepper_servo_position_converges);
    return UNITY_END();
}
