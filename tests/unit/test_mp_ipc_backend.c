/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file test_mp_ipc_backend.c
 * @brief BM_BUS_IPC over matrix payload 通道契约测试。
 *
 * 测试用 static bm_mp_ipc_matrix_t 直接 memset 构造（不走 matrix.c/bm_mp），
 * 分别验证 cmd_ring（FIFO）和 tel_channel（LATEST）语义。
 * 编译时须定义 BM_CONFIG_CPU_COUNT=2u（2×2 矩阵，source/target 均有效）。
 */
#include "unity.h"
#include "bm/core/bm_bus.h"
#include "bm/mp/bm_mp_ipc_backend.h"
#include "bm/common/bm_types.h"
#include <string.h>

/** @brief 命令消息类型（FIFO 测试） */
typedef struct { uint32_t v; } cmd_t;
/** @brief 遥测消息类型（LATEST 测试） */
typedef struct { uint32_t a, b; } tel_t;

/** @brief 共享矩阵：memset 全 0 构造，等价 bm_mp_ipc_matrix_format 后新通道部分 */
static bm_mp_ipc_matrix_t g_m;

void setUp(void)    { memset(&g_m, 0, sizeof(g_m)); }
void tearDown(void) {}

/* ================================================================== */
/* Task 2：FIFO over cmd_ring[0][1]                                    */
/* 验收：保序、满（DEPTH-1）、空三种边界                               */
/* ================================================================== */

/**
 * @brief FIFO over cmd_ring[0][1]：保序、满(DEPTH-1)、空。
 *
 * 1. 向 cmd_ring[0][1] 连续写入 DEPTH-1 个消息（填满）。
 * 2. 第 DEPTH 次写入应返回 BM_ERR_OVERFLOW。
 * 3. attach 读者，按序读出全部消息，验证值与写入顺序一致。
 * 4. 继续读时应返回 BM_ERR_WOULD_BLOCK（空）。
 */
void test_mp_fifo_order_full_empty(void)
{
    BM_BUS_DEFINE(b, cmd_t, 4u, 1u, BM_BUS_IPC);
    bm_bus_t h;
    bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    bm_mp_ipc_backend_ctx_t ctx;
    void *ws;
    const void *rs;

    TEST_ASSERT_EQUAL_INT(BM_OK,
        bm_mp_ipc_backend_open(&ctx, &g_m, 0u, 1u, sizeof(cmd_t)));
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_open(&h, &b_storage, &cfg));
    TEST_ASSERT_EQUAL_INT(BM_OK,
        bm_bus_bind_ipc_backend(&h, &g_mp_ipc_fifo_iface, &ctx));

    /* 填入 DEPTH-1 个消息 */
    for (uint32_t i = 0u; i < (BM_CONFIG_MP_IPC_CMD_RING_DEPTH - 1u); i++) {
        TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_acquire_write(&h, &ws));
        ((cmd_t *)ws)->v = i + 100u;
        TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_commit(&h));
    }
    /* 第 DEPTH 次写入：环满，应 OVERFLOW */
    TEST_ASSERT_EQUAL_INT(BM_ERR_OVERFLOW, bm_bus_acquire_write(&h, &ws));

    /* attach 读者并顺序读出 */
    bm_bus_reader_t r;
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_reader_attach(&h, &r));
    for (uint32_t i = 0u; i < (BM_CONFIG_MP_IPC_CMD_RING_DEPTH - 1u); i++) {
        TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_acquire_read(&r, &rs));
        TEST_ASSERT_EQUAL_UINT32(i + 100u, ((const cmd_t *)rs)->v);
        TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_release(&r));
    }
    /* 再读一次：环空，应 WOULD_BLOCK */
    TEST_ASSERT_EQUAL_INT(BM_ERR_WOULD_BLOCK, bm_bus_acquire_read(&r, &rs));
}

/* ================================================================== */
/* Task 3：LATEST over tel_channel[1][0]                              */
/* 验收：未发布→WOULD_BLOCK；发布后读最新；覆盖后读新值；同帧重复读   */
/* ================================================================== */

/**
 * @brief LATEST over tel_channel[1][0]：未发布、发布、覆盖、同帧重复读。
 *
 * 1. 首次 acquire_read 应返回 BM_ERR_WOULD_BLOCK（从未发布）。
 * 2. 发布 (a=1, b=2)，读出并校验。
 * 3. release 后同帧再次读（无新写）应仍返回 BM_OK 且值不变（可重复读最新值）。
 * 4. 发布 (a=9, b=8) 覆盖，读出并校验新值。
 */
void test_mp_latest_overwrite(void)
{
    BM_BUS_DEFINE(t, tel_t, 3u, 1u, BM_BUS_IPC);
    bm_bus_t h;
    bm_bus_cfg_t cfg = { .owner_cpu = 1u };
    bm_mp_ipc_backend_ctx_t ctx;
    void *ws;
    const void *rs;
    bm_bus_reader_t r;

    TEST_ASSERT_EQUAL_INT(BM_OK,
        bm_mp_ipc_backend_open(&ctx, &g_m, 1u, 0u, sizeof(tel_t)));
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_open(&h, &t_storage, &cfg));
    TEST_ASSERT_EQUAL_INT(BM_OK,
        bm_bus_bind_ipc_backend(&h, &g_mp_ipc_latest_iface, &ctx));
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_reader_attach(&h, &r));

    /* 从未发布：首次读应 WOULD_BLOCK */
    TEST_ASSERT_EQUAL_INT(BM_ERR_WOULD_BLOCK, bm_bus_acquire_read(&r, &rs));

    /* 发布第一帧 */
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_acquire_write(&h, &ws));
    ((tel_t *)ws)->a = 1u; ((tel_t *)ws)->b = 2u;
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_commit(&h));
    /* 读出并验证 */
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_acquire_read(&r, &rs));
    TEST_ASSERT_EQUAL_UINT32(1u, ((const tel_t *)rs)->a);
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_release(&r));

    /* 同帧重复读：无新写，再读仍 OK，值不变 */
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_acquire_read(&r, &rs));
    TEST_ASSERT_EQUAL_UINT32(1u, ((const tel_t *)rs)->a);
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_release(&r));

    /* 发布覆盖帧 */
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_acquire_write(&h, &ws));
    ((tel_t *)ws)->a = 9u; ((tel_t *)ws)->b = 8u;
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_commit(&h));
    /* 读出并验证新值 */
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_acquire_read(&r, &rs));
    TEST_ASSERT_EQUAL_UINT32(9u, ((const tel_t *)rs)->a);
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_release(&r));
}

/* ================================================================== */
/* main                                                                */
/* ================================================================== */

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_mp_fifo_order_full_empty);
    RUN_TEST(test_mp_latest_overwrite);
    return UNITY_END();
}
