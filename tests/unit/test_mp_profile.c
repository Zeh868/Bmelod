#include "unity.h"

#include "bm/mp/bm_mp.h"
#include "bm/mp/bm_mp_profile.h"
#include "bm/mp/bm_mp_schedule.h"
#include "bm_module.h"

#if BM_CONFIG_ENABLE_EXEC
#include "bm_exec.h"
#endif

const bm_module_t *_bm_module_table[1] = { NULL };
const uint32_t _bm_module_count = 0u;

void setUp(void) {
}

void tearDown(void) {
}

void test_hard_rt_profile_publication_order(void) {
#if BM_CONFIG_MP_HARD_RT_PROFILE
#if BM_CONFIG_ENABLE_EXEC
    static const bm_exec_t exec = {
        .id = 1u,
        .owner_cpu = 0u,
        .name = "profile_probe"
    };
    static const bm_exec_t *const execs[] = { &exec };
#endif
    bm_mp_ipc_matrix_t *matrix;

    TEST_ASSERT_EQUAL(BM_OK, bm_mp_init(BM_CONFIG_CPU_COUNT));
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_boot_bootstrap_sequence());
#if BM_CONFIG_ENABLE_EXEC
    bm_mp_profile_register_exec(execs, 1u);
#endif
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_profile_build());
    TEST_ASSERT_EQUAL(1, bm_mp_profile_is_built());

    matrix = bm_mp_ipc_matrix();
    TEST_ASSERT_NOT_NULL(matrix);
#if BM_MP_MULTICORE
    TEST_ASSERT_EQUAL(
        (uint32_t)BM_MP_BOOT_PROFILE_READY,
        bm_atomic_ipc_load_u32(&matrix->boot_phase));
#endif
    TEST_ASSERT_EQUAL(BM_ERR_ALREADY, bm_mp_profile_build());
#else
    TEST_PASS();
#endif
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_hard_rt_profile_publication_order);
    return UNITY_END();
}
