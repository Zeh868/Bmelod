/**
 * @file test_persist.c
 * @brief KV 持久化存储单元测试（路线图 #10）
 *
 * 依赖 native_sim 后端（BM_BACKEND=native_sim），通过文件仿真 NVS。
 *
 * 核心验证点（掉电语义）：
 *   set → commit → 重置后端（模拟掉电重启）→ init → get 读回一致
 *
 * 其余验证点：
 *   覆盖写（同 key 再 set）、erase 后 get 失败、不存在的 key、
 *   容量边界（值超长 / 键超长）、表满 BM_ERR_NO_MEM。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-26       1.0            zeh            正式发布（路线图 #10 参数/配置持久化）
 *
 */
#include "unity.h"
#include "bm_persist.h"
#include "bm_hal_nvs_native.h"

#include <string.h>
#include <stdint.h>

/** 测试用 NVS 文件路径（置于当前目录，tearDown 不删除，setUp 重置） */
#define TEST_NVS_PATH "_bm_persist_test.bin"

void setUp(void) {
    /* 确保每个测试从同一路径的干净文件状态启动 */
    bm_drv_nvs_native_set_path(TEST_NVS_PATH);
    bm_drv_nvs_native_reset();     /* 删除旧文件 */
    TEST_ASSERT_EQUAL(0, bm_persist_init()); /* 空表启动 */
}

void tearDown(void) {
    /* 清理测试文件，避免跨用例污染 */
    bm_drv_nvs_native_reset();
}

/* ========================================================================= */
/*  基础 RAM 读写                                                               */
/* ========================================================================= */

/**
 * @brief 未设置 key 时 get 返回 BM_ERR_NOT_FOUND
 */
void test_persist_get_not_found(void) {
    uint8_t buf[16];
    uint16_t len = 0u;

    TEST_ASSERT_EQUAL(BM_ERR_NOT_FOUND,
        bm_persist_get("missing", buf, sizeof(buf), &len));
}

/**
 * @brief set 后 get 能读回相同值（RAM 内）
 */
void test_persist_set_and_get(void) {
    uint32_t val_w = 0xDEADBEEFu;
    uint32_t val_r = 0u;
    uint16_t len   = 0u;

    TEST_ASSERT_EQUAL(0, bm_persist_set("magic", &val_w, sizeof(val_w)));
    TEST_ASSERT_EQUAL(0, bm_persist_get("magic", &val_r, sizeof(val_r), &len));
    TEST_ASSERT_EQUAL(sizeof(val_w), len);
    TEST_ASSERT_EQUAL_UINT32(val_w, val_r);
}

/**
 * @brief 同 key 再次 set 覆盖写，get 返回新值
 */
void test_persist_overwrite_same_key(void) {
    float v1 = 1.0f;
    float v2 = 3.14f;
    float vr = 0.0f;
    uint16_t len = 0u;

    TEST_ASSERT_EQUAL(0, bm_persist_set("kp", &v1, sizeof(v1)));
    TEST_ASSERT_EQUAL(0, bm_persist_set("kp", &v2, sizeof(v2)));
    TEST_ASSERT_EQUAL(0, bm_persist_get("kp", &vr, sizeof(vr), &len));
    /* 浮点比较：直接按字节比较，避免 NaN/精度问题 */
    TEST_ASSERT_EQUAL_MEMORY(&v2, &vr, sizeof(v2));
}

/**
 * @brief erase 后 get 返回 BM_ERR_NOT_FOUND
 */
void test_persist_erase_then_get_not_found(void) {
    uint8_t val = 42u;
    uint8_t buf = 0u;
    uint16_t len = 0u;

    TEST_ASSERT_EQUAL(0, bm_persist_set("flag", &val, sizeof(val)));
    TEST_ASSERT_EQUAL(0, bm_persist_erase("flag"));
    TEST_ASSERT_EQUAL(BM_ERR_NOT_FOUND,
        bm_persist_get("flag", &buf, sizeof(buf), &len));
}

/**
 * @brief erase 不存在的 key 返回 BM_ERR_NOT_FOUND
 */
void test_persist_erase_nonexistent_key(void) {
    TEST_ASSERT_EQUAL(BM_ERR_NOT_FOUND, bm_persist_erase("ghost"));
}

/**
 * @brief get 缓冲区不足时返回 BM_ERR_OVERFLOW
 */
void test_persist_get_overflow_cap(void) {
    uint32_t val = 0x12345678u;
    uint8_t  small_buf[2]; /* 容量不足 4 字节 */
    uint16_t len = 0u;

    TEST_ASSERT_EQUAL(0, bm_persist_set("num", &val, sizeof(val)));
    TEST_ASSERT_EQUAL(BM_ERR_OVERFLOW,
        bm_persist_get("num", small_buf, sizeof(small_buf), &len));
}

/**
 * @brief set 键名超长返回 BM_ERR_INVALID
 */
void test_persist_set_key_too_long(void) {
    /* BM_CONFIG_PERSIST_KEY_MAX_LEN 默认 15；构造 16 字节键名 */
    const char long_key[] = "1234567890123456"; /* 16 chars */
    uint8_t val = 1u;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID,
        bm_persist_set(long_key, &val, sizeof(val)));
}

/**
 * @brief set 值超长返回 BM_ERR_OVERFLOW
 */
void test_persist_set_val_too_long(void) {
    /* BM_CONFIG_PERSIST_VAL_MAX_LEN 默认 64；构造 65 字节值 */
    uint8_t big_val[65];
    uint16_t i;

    for (i = 0u; i < sizeof(big_val); i++) {
        big_val[i] = (uint8_t)i;
    }
    TEST_ASSERT_EQUAL(BM_ERR_OVERFLOW,
        bm_persist_set("big", big_val, sizeof(big_val)));
}

/**
 * @brief 空键名返回 BM_ERR_INVALID
 */
void test_persist_set_empty_key(void) {
    uint8_t val = 0u;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_persist_set("", &val, sizeof(val)));
}

/**
 * @brief NULL 参数返回 BM_ERR_INVALID
 */
void test_persist_null_param(void) {
    uint8_t val = 0u;
    uint16_t len = 0u;

    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_persist_set(NULL, &val, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_persist_set("k", NULL, 1u));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_persist_get(NULL, &val, 1u, &len));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_persist_get("k", NULL, 1u, &len));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_persist_get("k", &val, 1u, NULL));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_persist_erase(NULL));
}

/**
 * @brief 填满 BM_CONFIG_PERSIST_MAX_ENTRIES 个条目后再 set 返回 BM_ERR_NO_MEM
 *
 * 注：BM_CONFIG_PERSIST_MAX_ENTRIES 默认 16。
 */
void test_persist_table_full(void) {
    uint8_t val = 0u;
    int i;
    char key[8];
    int last_rc = 0;

    /* 填满表 */
    for (i = 0; i < 16; i++) {
        /* 生成唯一键名 "k00".."k15" */
        key[0] = 'k';
        key[1] = (char)('0' + i / 10);
        key[2] = (char)('0' + i % 10);
        key[3] = '\0';
        last_rc = bm_persist_set(key, &val, sizeof(val));
        if (last_rc != 0) {
            break;
        }
    }
    TEST_ASSERT_EQUAL(0, last_rc); /* 16 个应全部成功 */

    /* 第 17 个应失败 */
    TEST_ASSERT_EQUAL(BM_ERR_NO_MEM, bm_persist_set("overflow", &val, sizeof(val)));
}

/* ========================================================================= */
/*  掉电语义验证（核心）                                                        */
/* ========================================================================= */

/**
 * @brief 核心掉电语义：set→commit→重置并重新 init→get 读回一致
 *
 * 流程：
 * 1. set 多个键值；
 * 2. commit 落盘；
 * 3. bm_persist_init() 模拟掉电重启（从文件重载）；
 * 4. get 各键，断言与写入值完全一致。
 */
void test_persist_power_cycle_basic(void) {
    uint32_t val_u32 = 0xCAFEBABEu;
    float    val_f32 = 2.718f;
    uint8_t  buf8[4];
    float    buf_f;
    uint16_t len = 0u;

    /* 写入两个键 */
    TEST_ASSERT_EQUAL(0, bm_persist_set("counter", &val_u32, sizeof(val_u32)));
    TEST_ASSERT_EQUAL(0, bm_persist_set("gain",    &val_f32, sizeof(val_f32)));
    /* 落盘 */
    TEST_ASSERT_EQUAL(0, bm_persist_commit());

    /* 模拟掉电重启：重新加载（不删除文件） */
    TEST_ASSERT_EQUAL(0, bm_persist_init());

    /* 验证 counter */
    TEST_ASSERT_EQUAL(0,
        bm_persist_get("counter", buf8, sizeof(buf8), &len));
    TEST_ASSERT_EQUAL(sizeof(val_u32), len);
    TEST_ASSERT_EQUAL_MEMORY(&val_u32, buf8, sizeof(val_u32));

    /* 验证 gain */
    TEST_ASSERT_EQUAL(0,
        bm_persist_get("gain", &buf_f, sizeof(buf_f), &len));
    TEST_ASSERT_EQUAL(sizeof(val_f32), len);
    TEST_ASSERT_EQUAL_MEMORY(&val_f32, &buf_f, sizeof(val_f32));
}

/**
 * @brief erase 后 commit，掉电重启后 get 返回 BM_ERR_NOT_FOUND
 */
void test_persist_power_cycle_erase(void) {
    uint8_t  val = 99u;
    uint8_t  buf = 0u;
    uint16_t len = 0u;

    TEST_ASSERT_EQUAL(0, bm_persist_set("temp", &val, sizeof(val)));
    TEST_ASSERT_EQUAL(0, bm_persist_commit());

    /* 确认写入成功 */
    TEST_ASSERT_EQUAL(0, bm_persist_get("temp", &buf, sizeof(buf), &len));

    /* 删除并落盘 */
    TEST_ASSERT_EQUAL(0, bm_persist_erase("temp"));
    TEST_ASSERT_EQUAL(0, bm_persist_commit());

    /* 掉电重启 */
    TEST_ASSERT_EQUAL(0, bm_persist_init());

    /* 应不存在 */
    TEST_ASSERT_EQUAL(BM_ERR_NOT_FOUND,
        bm_persist_get("temp", &buf, sizeof(buf), &len));
}

/**
 * @brief 覆盖写后 commit，掉电重启后 get 返回最新值
 */
void test_persist_power_cycle_overwrite(void) {
    uint16_t v1  = 100u;
    uint16_t v2  = 200u;
    uint16_t vr  = 0u;
    uint16_t len = 0u;

    TEST_ASSERT_EQUAL(0, bm_persist_set("setpoint", &v1, sizeof(v1)));
    TEST_ASSERT_EQUAL(0, bm_persist_commit());

    /* 覆盖写并重新落盘 */
    TEST_ASSERT_EQUAL(0, bm_persist_set("setpoint", &v2, sizeof(v2)));
    TEST_ASSERT_EQUAL(0, bm_persist_commit());

    /* 掉电重启 */
    TEST_ASSERT_EQUAL(0, bm_persist_init());

    TEST_ASSERT_EQUAL(0,
        bm_persist_get("setpoint", &vr, sizeof(vr), &len));
    TEST_ASSERT_EQUAL(sizeof(v2), len);
    TEST_ASSERT_EQUAL_UINT16(v2, vr);
}

/**
 * @brief 首次上电（无文件）init 后 get 返回 BM_ERR_NOT_FOUND（正常空表）
 */
void test_persist_first_boot_empty(void) {
    /* setUp 已删除文件并调用 init，表应为空 */
    uint8_t  buf = 0u;
    uint16_t len = 0u;

    TEST_ASSERT_EQUAL(BM_ERR_NOT_FOUND,
        bm_persist_get("any_key", &buf, sizeof(buf), &len));
}

/**
 * @brief 未 commit 的数据掉电后不存在（未落盘语义）
 */
void test_persist_uncommitted_data_lost_on_power_cycle(void) {
    uint8_t  val  = 55u;
    uint8_t  buf  = 0u;
    uint16_t len  = 0u;

    /* set 但不 commit */
    TEST_ASSERT_EQUAL(0, bm_persist_set("volatile_key", &val, sizeof(val)));
    /* RAM 中应可读 */
    TEST_ASSERT_EQUAL(0,
        bm_persist_get("volatile_key", &buf, sizeof(buf), &len));
    TEST_ASSERT_EQUAL_UINT8(val, buf);

    /* 模拟掉电重启（未 commit，文件不存在） */
    TEST_ASSERT_EQUAL(0, bm_persist_init());

    /* 应不存在（数据丢失，符合预期） */
    TEST_ASSERT_EQUAL(BM_ERR_NOT_FOUND,
        bm_persist_get("volatile_key", &buf, sizeof(buf), &len));
}

/* ========================================================================= */
/*  边界：最大容量值                                                             */
/* ========================================================================= */

/**
 * @brief 存储并读回最大长度（64 字节）的值
 */
void test_persist_max_value_length(void) {
    uint8_t  big[64];
    uint8_t  rbuf[64];
    uint16_t len = 0u;
    uint16_t i;

    for (i = 0u; i < 64u; i++) {
        big[i] = (uint8_t)(i ^ 0xA5u);
    }
    TEST_ASSERT_EQUAL(0, bm_persist_set("blob64", big, 64u));
    TEST_ASSERT_EQUAL(0, bm_persist_get("blob64", rbuf, 64u, &len));
    TEST_ASSERT_EQUAL(64u, len);
    TEST_ASSERT_EQUAL_MEMORY(big, rbuf, 64u);
}

/**
 * @brief 掉电后最大长度值仍能读回
 */
void test_persist_power_cycle_max_value(void) {
    uint8_t  big[64];
    uint8_t  rbuf[64];
    uint16_t len = 0u;
    uint16_t i;

    for (i = 0u; i < 64u; i++) {
        big[i] = (uint8_t)(i * 3u);
    }
    TEST_ASSERT_EQUAL(0, bm_persist_set("bigkey", big, 64u));
    TEST_ASSERT_EQUAL(0, bm_persist_commit());

    /* 模拟掉电重启 */
    TEST_ASSERT_EQUAL(0, bm_persist_init());

    TEST_ASSERT_EQUAL(0, bm_persist_get("bigkey", rbuf, 64u, &len));
    TEST_ASSERT_EQUAL(64u, len);
    TEST_ASSERT_EQUAL_MEMORY(big, rbuf, 64u);
}

/* ========================================================================= */
/*  主函数                                                                      */
/* ========================================================================= */

int main(void) {
    UNITY_BEGIN();

    /* 基础 RAM KV */
    RUN_TEST(test_persist_get_not_found);
    RUN_TEST(test_persist_set_and_get);
    RUN_TEST(test_persist_overwrite_same_key);
    RUN_TEST(test_persist_erase_then_get_not_found);
    RUN_TEST(test_persist_erase_nonexistent_key);
    RUN_TEST(test_persist_get_overflow_cap);
    RUN_TEST(test_persist_set_key_too_long);
    RUN_TEST(test_persist_set_val_too_long);
    RUN_TEST(test_persist_set_empty_key);
    RUN_TEST(test_persist_null_param);
    RUN_TEST(test_persist_table_full);

    /* 掉电语义验证（核心） */
    RUN_TEST(test_persist_power_cycle_basic);
    RUN_TEST(test_persist_power_cycle_erase);
    RUN_TEST(test_persist_power_cycle_overwrite);
    RUN_TEST(test_persist_first_boot_empty);
    RUN_TEST(test_persist_uncommitted_data_lost_on_power_cycle);

    /* 边界：最大容量值 */
    RUN_TEST(test_persist_max_value_length);
    RUN_TEST(test_persist_power_cycle_max_value);

    return UNITY_END();
}
