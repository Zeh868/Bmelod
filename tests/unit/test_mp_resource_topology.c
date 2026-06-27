/**
 * @file test_mp_resource_topology.c
 * @brief 资源拓扑注册与亲和闭包校验单元测试
 *
 * 覆盖 bm_mp_resource_topology 的注册（成功/空/零掩码/越界/分区后冻结）、
 * claim 校验（空/owner 越界/命中成功/owner 不在掩码/歧义掩码/未注册 app/HAL）、
 * 拓扑表校验与 exec 表亲和校验。该文件此前几乎无覆盖。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            正式发布
 *
 */
#include "unity.h"
#include "bm/mp/bm_mp.h"
#include "bm/mp/bm_mp_partition.h"
#include "bm/mp/bm_mp_resource_topology.h"
#include "bm/hybrid/bm_resource.h"
#include "bm_module.h"
#include "bm_hal_cpu_mp_native.h"

#include <string.h>

/* 分区 build 会拖入对应用模块表的引用；提供不被调用的最小表以满足链接。 */
static int rtopo_dummy_init(void) {
    return BM_OK;
}
BM_MODULE_DEFINE(rtopo_dummy, 0u, rtopo_dummy_init, NULL, NULL, NULL);
BM_MODULE_TABLE(BM_MODULE_ENTRY(rtopo_dummy));

#define HAL_CLASS ((bm_resource_class_t)1u)
#define APP_CLASS ((bm_resource_class_t)(BM_RESOURCE_CLASS_APP_BASE + 1u))

void setUp(void) {
    bm_hal_cpu_init();
    (void)bm_hal_cpu_native_set_id(0u);
    bm_mp_partition_reset();
    bm_mp_resource_topology_reset();
}

void tearDown(void) {
    bm_mp_resource_topology_reset();
    bm_mp_partition_reset();
}

static bm_mp_resource_topology_entry_t make_entry(uint32_t mask,
                                                  uintptr_t key) {
    bm_mp_resource_topology_entry_t e;

    memset(&e, 0, sizeof(e));
    e.resource_class = HAL_CLASS;
    e.key = key;
    e.allowed_cpu_mask = mask;
    return e;
}

void test_topology_register_validates_args(void) {
    bm_mp_resource_topology_entry_t e = make_entry(0x1u, 0x100u);

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_mp_resource_topology_register(NULL));
    e.allowed_cpu_mask = 0u;
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_mp_resource_topology_register(&e));
    e.allowed_cpu_mask = 0x1u;
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_resource_topology_register(&e));
}

void test_topology_register_full_returns_no_mem(void) {
    bm_mp_resource_topology_entry_t e = make_entry(0x1u, 0u);
    uint32_t i;

    for (i = 0u; i < BM_CONFIG_MP_RESOURCE_TOPOLOGY_MAX; i++) {
        e.key = (uintptr_t)(0x1000u + i);
        TEST_ASSERT_EQUAL(BM_OK, bm_mp_resource_topology_register(&e));
    }
    e.key = 0xDEADu;
    TEST_ASSERT_EQUAL(BM_ERR_NO_MEM, bm_mp_resource_topology_register(&e));
}

void test_topology_register_frozen_after_partition_build(void) {
    bm_mp_resource_topology_entry_t e = make_entry(0x1u, 0x200u);

    TEST_ASSERT_EQUAL(BM_OK,
                      bm_mp_partition_register_event_owner(0u, "evt0", 0u));
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_partition_build_and_validate());
    TEST_ASSERT_NOT_NULL(bm_mp_partition());

    TEST_ASSERT_EQUAL(BM_ERR_BUSY, bm_mp_resource_topology_register(&e));
}

void test_validate_claim_rejects_bad_args(void) {
    bm_resource_claim_t claim;

    memset(&claim, 0, sizeof(claim));
    claim.resource_class = HAL_CLASS;
    claim.key = 0x100u;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_mp_resource_topology_validate_claim(0u, NULL));
    TEST_ASSERT_EQUAL(
        BM_ERR_INVALID,
        bm_mp_resource_topology_validate_claim(BM_CONFIG_CPU_COUNT, &claim));
}

void test_validate_claim_matches_single_owner(void) {
    bm_mp_resource_topology_entry_t e = make_entry(0x1u /* cpu0 */, 0x100u);
    bm_resource_claim_t claim;

    TEST_ASSERT_EQUAL(BM_OK, bm_mp_resource_topology_register(&e));
    memset(&claim, 0, sizeof(claim));
    claim.resource_class = HAL_CLASS;
    claim.key = 0x100u;
    claim.name = "uart0";

    TEST_ASSERT_EQUAL(BM_OK,
                      bm_mp_resource_topology_validate_claim(0u, &claim));
    /* owner cpu1 不在 mask(=cpu0) 中 */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_mp_resource_topology_validate_claim(1u, &claim));
}

void test_validate_claim_rejects_ambiguous_mask(void) {
    bm_mp_resource_topology_entry_t e =
        make_entry(0x3u /* cpu0+cpu1 */, 0x100u);
    bm_resource_claim_t claim;

    TEST_ASSERT_EQUAL(BM_OK, bm_mp_resource_topology_register(&e));
    memset(&claim, 0, sizeof(claim));
    claim.resource_class = HAL_CLASS;
    claim.key = 0x100u;
    claim.name = "ambig";

    /* owner 在掩码内，但掩码含多核 → 归属歧义 */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_mp_resource_topology_validate_claim(0u, &claim));
}

void test_validate_claim_unregistered_app_class_is_ok(void) {
    bm_mp_resource_topology_entry_t e = make_entry(0x1u, 0x100u);
    bm_resource_claim_t claim;

    TEST_ASSERT_EQUAL(BM_OK, bm_mp_resource_topology_register(&e));
    memset(&claim, 0, sizeof(claim));
    claim.resource_class = APP_CLASS; /* >= APP_BASE，纯软件 claim */
    claim.key = 0x999u;               /* 未注册 key */
    claim.name = "soft";

    TEST_ASSERT_EQUAL(BM_OK,
                      bm_mp_resource_topology_validate_claim(0u, &claim));
}

void test_validate_claim_empty_table_is_ok(void) {
    bm_resource_claim_t claim;

    memset(&claim, 0, sizeof(claim));
    claim.resource_class = HAL_CLASS;
    claim.key = 0x100u;

    /* 无任何注册项时不强制校验 */
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_mp_resource_topology_validate_claim(0u, &claim));
}

void test_validate_claim_unregistered_hal_soft_profile_is_ok(void) {
    bm_mp_resource_topology_entry_t e = make_entry(0x1u, 0x100u);
    bm_resource_claim_t claim;

    TEST_ASSERT_EQUAL(BM_OK, bm_mp_resource_topology_register(&e));
    memset(&claim, 0, sizeof(claim));
    claim.resource_class = HAL_CLASS;
    claim.key = 0x7777u; /* 未注册 HAL key */
    claim.name = "unreg_hal";

#if BM_CONFIG_MP_HARD_RT_PROFILE
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_mp_resource_topology_validate_claim(0u, &claim));
#else
    /* 软剖面下未注册 HAL claim 仅告警，放行 */
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_mp_resource_topology_validate_claim(0u, &claim));
#endif
}

void test_validate_table_detects_ambiguous_entry(void) {
    bm_mp_resource_topology_entry_t ok = make_entry(0x2u /* cpu1 */, 0x100u);
    bm_mp_resource_topology_entry_t bad = make_entry(0x3u /* ambig */, 0x200u);

    TEST_ASSERT_EQUAL(BM_OK, bm_mp_resource_topology_register(&ok));
    TEST_ASSERT_EQUAL(BM_OK, bm_mp_resource_topology_validate_table());

    TEST_ASSERT_EQUAL(BM_OK, bm_mp_resource_topology_register(&bad));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_mp_resource_topology_validate_table());
}

#if BM_CONFIG_ENABLE_EXEC
void test_validate_exec_table_args_and_owner(void) {
    bm_exec_t inst;
    const bm_exec_t *instances[1];

    /* instances 为空但 count>0 → 非法 */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
                      bm_mp_resource_topology_validate_exec_table(NULL, 1u));
    /* count==0 → 平凡通过 */
    TEST_ASSERT_EQUAL(BM_OK,
                      bm_mp_resource_topology_validate_exec_table(NULL, 0u));

    memset(&inst, 0, sizeof(inst));
    inst.owner_cpu = 0u;
    instances[0] = &inst;
    TEST_ASSERT_EQUAL(
        BM_OK,
        bm_mp_resource_topology_validate_exec_table(instances, 1u));

    inst.owner_cpu = BM_CONFIG_CPU_COUNT; /* 越界 owner */
    TEST_ASSERT_EQUAL(
        BM_ERR_INVALID,
        bm_mp_resource_topology_validate_exec_table(instances, 1u));
}
#endif

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_topology_register_validates_args);
    RUN_TEST(test_topology_register_full_returns_no_mem);
    RUN_TEST(test_topology_register_frozen_after_partition_build);
    RUN_TEST(test_validate_claim_rejects_bad_args);
    RUN_TEST(test_validate_claim_matches_single_owner);
    RUN_TEST(test_validate_claim_rejects_ambiguous_mask);
    RUN_TEST(test_validate_claim_unregistered_app_class_is_ok);
    RUN_TEST(test_validate_claim_empty_table_is_ok);
    RUN_TEST(test_validate_claim_unregistered_hal_soft_profile_is_ok);
    RUN_TEST(test_validate_table_detects_ambiguous_entry);
#if BM_CONFIG_ENABLE_EXEC
    RUN_TEST(test_validate_exec_table_args_and_owner);
#endif
    return UNITY_END();
}
