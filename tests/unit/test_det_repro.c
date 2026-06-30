/**
 * @file test_det_repro.c
 * @brief L1 功能确定性——可复现性单测（A1 纯函数 + A2 有状态）
 *
 * 覆盖确定性验证方案 spec §4.1 可复现性维度：
 *   A1：纯函数 clarke / clarke_2shunt 同输入 N 遍逐位相同；
 *   A2：一阶 LPF1 有状态路径同输入序列两遍跑，逐步 memcmp 状态+输出。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-30
 */
#include "unity.h"
#include "bm/algorithm/bm_algo_motor.h"
#include "bm/algorithm/bm_algo_filter.h"
#include <stdint.h>
#include <string.h>

/** 可复现性重复次数（同输入跑 N 遍逐位比对） */
#define DET_REPRO_RUNS       64u
/** 有状态路径输入序列长度 */
#define DET_REPRO_INPUT_LEN  20u

void setUp(void)    {}
void tearDown(void) {}

/**
 * @brief 通用逐位可复现性断言：对 size 字节的两块内存逐位 memcmp
 * @param first  首遍输出缓冲（已跑一次，只读）
 * @param again  再跑一次的输出缓冲（只读）
 * @param size   比对字节数
 */
static void det_assert_bitwise_equal(const void *first, const void *again, size_t size)
{
    TEST_ASSERT_EQUAL_INT(0, memcmp(first, again, size));
}

/** @brief 骨架自检：套件可编译可运行 */
void test_det_suite_boots(void)
{
    TEST_ASSERT_TRUE(1);
}

/** @brief A1-a：clarke 纯函数同输入 DET_REPRO_RUNS 遍逐位可复现 */
void test_det_repro_clarke(void)
{
    bm_algo_abc_t in;
    bm_algo_alphabeta_t first;
    bm_algo_alphabeta_t again;
    uint32_t i;

    in.ia = 1.2345f;
    in.ib = -2.3456f;
    in.ic = 0.0f;

    memset(&first, 0, sizeof(first));
    bm_algo_clarke(&in, &first);

    for (i = 0u; i < DET_REPRO_RUNS; i++) {
        memset(&again, 0, sizeof(again));
        bm_algo_clarke(&in, &again);
        det_assert_bitwise_equal(&first, &again, sizeof(bm_algo_alphabeta_t));
    }
}

/** @brief A1-b：clarke_2shunt 纯函数同输入 DET_REPRO_RUNS 遍逐位可复现 */
void test_det_repro_clarke_2shunt(void)
{
    bm_algo_alphabeta_t first;
    bm_algo_alphabeta_t again;
    uint32_t i;

    memset(&first, 0, sizeof(first));
    bm_algo_clarke_2shunt(0.777f, -0.333f, &first);

    for (i = 0u; i < DET_REPRO_RUNS; i++) {
        memset(&again, 0, sizeof(again));
        bm_algo_clarke_2shunt(0.777f, -0.333f, &again);
        det_assert_bitwise_equal(&first, &again, sizeof(bm_algo_alphabeta_t));
    }
}

/**
 * @brief A2：LPF1 有状态路径同输入序列两遍跑，逐步 memcmp 状态与输出。
 *
 * 证明含内部状态的有状态路径在同初始条件、同输入序列下，
 * 每一步状态快照逐位相同（spec §4.1 可复现性，有状态维度）。
 * 若滤波器内部有未初始化字段或数据依赖的非确定路径，
 * 某步 memcmp 会失败，测试立即红。
 */
void test_det_repro_lpf1_state_trajectory(void)
{
    /* 固定输入序列：覆盖正负过零、阶跃后平稳等典型轨迹 */
    static const float inputs[DET_REPRO_INPUT_LEN] = {
        1.0f,  0.8f,  0.5f,  0.2f, -0.1f,
       -0.4f, -0.7f, -1.0f, -0.8f, -0.5f,
        0.0f,  0.3f,  0.6f,  0.9f,  1.2f,
        1.0f,  0.7f,  0.3f, -0.2f, -0.8f
    };
    bm_algo_lpf1_config_t cfg;
    bm_algo_lpf1_state_t  s1;
    bm_algo_lpf1_state_t  s2;
    bm_algo_lpf1_state_t  snap1[DET_REPRO_INPUT_LEN];
    bm_algo_lpf1_state_t  snap2[DET_REPRO_INPUT_LEN];
    float                 out1;
    float                 out2;
    uint32_t              i;

    /* 10 Hz 截止频率，1 kHz 采样率；返回值 0 = 成功 */
    memset(&cfg, 0, sizeof(cfg));
    TEST_ASSERT_EQUAL_INT(0, bm_algo_lpf1_init_from_cutoff(&cfg, 10.0f, 1000.0f));

    /* 第一遍：从 0 初始值跑完序列，逐步保存状态快照 */
    bm_algo_lpf1_reset(&s1, 0.0f);
    for (i = 0u; i < DET_REPRO_INPUT_LEN; i++) {
        out1 = bm_algo_lpf1_step(&s1, &cfg, inputs[i]);
        snap1[i] = s1;
        (void)out1; /* 输出值经由 state.output 保存，此处不需额外用 */
    }

    /* 第二遍：相同初始条件，逐步比对状态 */
    bm_algo_lpf1_reset(&s2, 0.0f);
    for (i = 0u; i < DET_REPRO_INPUT_LEN; i++) {
        out2 = bm_algo_lpf1_step(&s2, &cfg, inputs[i]);
        snap2[i] = s2;
        (void)out2;
        /* 每步状态逐位相同 */
        det_assert_bitwise_equal(&snap1[i], &snap2[i], sizeof(bm_algo_lpf1_state_t));
    }
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_det_suite_boots);
    RUN_TEST(test_det_repro_clarke);
    RUN_TEST(test_det_repro_clarke_2shunt);
    RUN_TEST(test_det_repro_lpf1_state_trajectory);
    return UNITY_END();
}
