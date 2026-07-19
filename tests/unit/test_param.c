/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_param.c
 * @brief bm_param 参数注册表单元测试（批 P）
 *
 * 依赖 native_sim 后端（BM_BACKEND=native_sim），通过文件仿真 NVS
 * 验证 bm_param 与 bm_persist 的落盘/overlay 往返语义。
 *
 * 核心验证点：
 *   注册校验（非法表拒绝）、登记后镜像=出厂默认（不 apply）、
 *   set 热写 ptr+apply、REBOOT 标志延迟生效、save/load_overlay 往返、
 *   reset guard 拦截与 reset 后回出厂默认。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-11
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-11       1.0            zeh            正式发布（批 P：bm_param 参数注册表单测）
 * 2026-07-11       1.1            zeh            同步 min/max 新布局；新增值域校验用例
 *
 */
#include "unity.h"
#include "bm/core/bm_param.h"
#include "bm_persist.h"
#include "bm_hal_nvs_native.h"

#include <string.h>

#define TEST_NVS_PATH "_bm_param_test.bin"

/* 直写目标 + apply 回调记录（每个用 ptr 的表项独立落点——
 * 多表项共享同一 ptr 落点属登记方错误用法，组件不承诺仲裁顺序） */
static float s_ptr_a;   /* t.ptr 落点 */
static float s_ptr_c;   /* t.ram 落点 */
static float s_ptr_d;   /* t.boot 落点 */
static float s_ptr_r;   /* t.rng 落点（有界项） */
static float s_apply_val;
static int   s_apply_calls;
static int   s_guard_block;

static void t_apply(float val, void *user) {
    (void)user;
    s_apply_val = val;
    s_apply_calls++;
}

static int t_guard(void) { return s_guard_block; }

static const bm_param_desc_t k_tbl[] = {
    { "t.ptr",  1.5f, 0.0f, 0.0f,  &s_ptr_a, NULL,    NULL, "t.ptr",  0u },
    { "t.cb",   2.5f, 0.0f, 0.0f,  NULL,     t_apply, NULL, "t.cb",   0u },
    { "t.ram",  3.5f, 0.0f, 0.0f,  &s_ptr_c, NULL,    NULL, NULL,     0u },
    { "t.boot", 4.5f, 0.0f, 0.0f,  &s_ptr_d, NULL,    NULL, "t.boot", BM_PARAM_FLAG_REBOOT },
    { "t.rng",  5.0f, 0.0f, 10.0f, &s_ptr_r, NULL,    NULL, "t.rng",  0u },
};
#define K_TBL_N ((uint16_t)(sizeof(k_tbl) / sizeof(k_tbl[0])))

void setUp(void) {
    bm_drv_nvs_native_set_path(TEST_NVS_PATH);
    bm_drv_nvs_native_reset();
    TEST_ASSERT_EQUAL(BM_OK, bm_persist_init());
    s_ptr_a = 0.0f;
    s_ptr_c = 0.0f;
    s_ptr_d = 0.0f;
    s_ptr_r = 0.0f;
    s_apply_val = 0.0f;
    s_apply_calls = 0;
    s_guard_block = 0;
    bm_param_set_reset_guard(NULL);
    TEST_ASSERT_EQUAL(BM_OK, bm_param_register_table(k_tbl, K_TBL_N));
}

void tearDown(void) { bm_drv_nvs_native_reset(); }

void test_param_register_rejects_invalid(void) {
    static const bm_param_desc_t bad[] = {
        { "x", 0.0f, 0.0f, 0.0f, NULL, NULL, NULL, NULL, 0u },   /* ptr/apply 都空 */
    };
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_param_register_table(NULL, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_param_register_table(k_tbl, 0u));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_param_register_table(bad, 1u));
    /* 恢复正常表供后续断言 */
    TEST_ASSERT_EQUAL(BM_OK, bm_param_register_table(k_tbl, K_TBL_N));
    TEST_ASSERT_EQUAL(K_TBL_N, bm_param_count());
}

void test_param_defaults_after_register(void) {
    float v = 0.0f;
    TEST_ASSERT_EQUAL(BM_OK, bm_param_get("t.ptr", &v));
    TEST_ASSERT_EQUAL_FLOAT(1.5f, v);
    TEST_ASSERT_EQUAL(0, s_apply_calls);          /* 登记不 apply */
    TEST_ASSERT_EQUAL(BM_ERR_NOT_FOUND, bm_param_get("nope", &v));
}

void test_param_set_writes_ptr_and_apply(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_param_set("t.ptr", 7.25f));
    TEST_ASSERT_EQUAL_FLOAT(7.25f, s_ptr_a);
    TEST_ASSERT_EQUAL(BM_OK, bm_param_set("t.cb", 8.5f));
    TEST_ASSERT_EQUAL_FLOAT(8.5f, s_apply_val);
    TEST_ASSERT_EQUAL(1, s_apply_calls);
    TEST_ASSERT_EQUAL(BM_ERR_NOT_FOUND, bm_param_set("nope", 1.0f));
}

void test_param_reboot_flag_defers_apply(void) {
    s_ptr_d = 0.0f;
    TEST_ASSERT_EQUAL(BM_PARAM_REBOOT_REQUIRED, bm_param_set("t.boot", 9.0f));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s_ptr_d);     /* 未热写 */
    float v = 0.0f;
    TEST_ASSERT_EQUAL(BM_OK, bm_param_get("t.boot", &v));
    TEST_ASSERT_EQUAL_FLOAT(9.0f, v);            /* 镜像已记录 */
}

void test_param_save_then_overlay_roundtrip(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_param_set("t.ptr", 6.0f));
    TEST_ASSERT_EQUAL(4, bm_param_save());        /* pkey 非空共 4 项（含 t.rng） */

    /* 模拟重启：重新登记（镜像回 def）→ overlay 读回 */
    TEST_ASSERT_EQUAL(BM_OK, bm_param_register_table(k_tbl, K_TBL_N));
    s_ptr_a = 0.0f;
    TEST_ASSERT_EQUAL(4, bm_param_load_overlay());
    float v = 0.0f;
    TEST_ASSERT_EQUAL(BM_OK, bm_param_get("t.ptr", &v));
    TEST_ASSERT_EQUAL_FLOAT(6.0f, v);
    TEST_ASSERT_EQUAL_FLOAT(6.0f, s_ptr_a);       /* overlay 走 apply 路 */
}

void test_param_reset_guard_and_restore(void) {
    TEST_ASSERT_EQUAL(BM_OK, bm_param_set("t.ptr", 6.0f));
    TEST_ASSERT_TRUE(bm_param_save() >= 0);

    bm_param_set_reset_guard(t_guard);
    s_guard_block = 1;
    TEST_ASSERT_EQUAL(BM_ERR_BUSY, bm_param_reset());

    s_guard_block = 0;
    TEST_ASSERT_EQUAL(BM_OK, bm_param_reset());
    float v = 0.0f;
    TEST_ASSERT_EQUAL(BM_OK, bm_param_get("t.ptr", &v));
    TEST_ASSERT_EQUAL_FLOAT(1.5f, v);             /* 回出厂 */
    TEST_ASSERT_EQUAL_FLOAT(1.5f, s_ptr_a);       /* reset 逐参数 apply */
    uint8_t raw[8]; uint16_t len = 0u;
    TEST_ASSERT_EQUAL(BM_ERR_NOT_FOUND,
        bm_persist_get("t.ptr", raw, sizeof(raw), &len)); /* KV 已清 */
}

/** @brief reset 期间 commit 失败后须回滚旧值，避免 flash/RAM 分叉。 */
void test_param_reset_rollback_on_commit_failure(void) {
    float v = 0.0f;

    TEST_ASSERT_EQUAL(BM_OK, bm_param_set("t.ptr", 6.0f));
    TEST_ASSERT_TRUE(bm_param_save() >= 0);

    /* 注入：下一次 save（reset 内部的 commit）强制失败 */
    bm_drv_nvs_native_set_fail_save_count(1);
    TEST_ASSERT_EQUAL(BM_ERR_OVERFLOW, bm_param_reset());

    /* 回滚后 persist 应仍保留旧值 6.0f，而非被擦除或回出厂 */
    TEST_ASSERT_EQUAL(BM_OK, bm_param_get("t.ptr", &v));
    TEST_ASSERT_EQUAL_FLOAT(6.0f, v);
    uint8_t raw[8]; uint16_t len = 0u;
    TEST_ASSERT_EQUAL(BM_OK,
        bm_persist_get("t.ptr", raw, sizeof(raw), &len));
    TEST_ASSERT_EQUAL_UINT16((uint16_t)sizeof(float), len);
    TEST_ASSERT_EQUAL_FLOAT(6.0f, *(const float *)(const void *)raw);
}

/** @brief NaN/Inf 恒被拒（含无界逃生口项）。 */
void test_param_set_rejects_non_finite(void) {
    union { uint32_t u; float f; } nan_u; nan_u.u = 0x7FC00000u;
    union { uint32_t u; float f; } inf_u; inf_u.u = 0x7F800000u;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_param_set("t.ptr", nan_u.f)); /* min==max 无界项也拒 NaN */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_param_set("t.rng", inf_u.f));
    float v = 0.0f;
    TEST_ASSERT_EQUAL(BM_OK, bm_param_get("t.ptr", &v));
    TEST_ASSERT_EQUAL_FLOAT(1.5f, v); /* 镜像未被污染 */
}

/** @brief 有界项越界被拒、界内通过；无界项任意有限值通过。 */
void test_param_set_range_enforced(void) {
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_param_set("t.rng", 10.5f));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_param_set("t.rng", -0.1f));
    TEST_ASSERT_EQUAL(BM_OK, bm_param_set("t.rng", 10.0f));  /* 闭区间上界 */
    TEST_ASSERT_EQUAL(BM_OK, bm_param_set("t.ptr", 1.0e9f)); /* 无界项 */
}

/** @brief overlay 坏值跳过：不 apply、不计数、镜像保持出厂。 */
void test_param_overlay_skips_out_of_range(void) {
    float bad = 999.0f;
    float good = 6.0f;

    TEST_ASSERT_EQUAL(BM_OK, bm_persist_set("t.rng", &bad, sizeof(bad)));
    TEST_ASSERT_EQUAL(BM_OK, bm_persist_set("t.ptr", &good, sizeof(good)));
    s_ptr_r = 0.0f;
    TEST_ASSERT_EQUAL(1, bm_param_load_overlay()); /* 只有 t.ptr 命中 */
    float v = 0.0f;
    TEST_ASSERT_EQUAL(BM_OK, bm_param_get("t.rng", &v));
    TEST_ASSERT_EQUAL_FLOAT(5.0f, v);              /* 保持出厂 */
    TEST_ASSERT_EQUAL_FLOAT(0.0f, s_ptr_r);        /* 未 apply */
    TEST_ASSERT_EQUAL(BM_OK, bm_param_get("t.ptr", &v));
    TEST_ASSERT_EQUAL_FLOAT(6.0f, v);
}

/** @brief 注册期挡表作者笔误：def 越界 / min>max。 */
void test_param_register_rejects_bad_bounds(void) {
    static float dst;
    static const bm_param_desc_t bad_def[] = {
        { "x", 99.0f, 0.0f, 10.0f, &dst, NULL, NULL, NULL, 0u },
    };
    static const bm_param_desc_t bad_minmax[] = {
        { "y", 1.0f, 10.0f, 0.0f, &dst, NULL, NULL, NULL, 0u },
    };
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_param_register_table(bad_def, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_param_register_table(bad_minmax, 1u));
    TEST_ASSERT_EQUAL(BM_OK, bm_param_register_table(k_tbl, K_TBL_N)); /* 恢复 */
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_param_register_rejects_invalid);
    RUN_TEST(test_param_defaults_after_register);
    RUN_TEST(test_param_set_writes_ptr_and_apply);
    RUN_TEST(test_param_reboot_flag_defers_apply);
    RUN_TEST(test_param_save_then_overlay_roundtrip);
    RUN_TEST(test_param_reset_guard_and_restore);
    RUN_TEST(test_param_reset_rollback_on_commit_failure);
    RUN_TEST(test_param_set_rejects_non_finite);
    RUN_TEST(test_param_set_range_enforced);
    RUN_TEST(test_param_overlay_skips_out_of_range);
    RUN_TEST(test_param_register_rejects_bad_bounds);
    return UNITY_END();
}
