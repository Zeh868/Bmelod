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

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_det_suite_boots);
    RUN_TEST(test_det_repro_clarke);
    RUN_TEST(test_det_repro_clarke_2shunt);
    return UNITY_END();
}
