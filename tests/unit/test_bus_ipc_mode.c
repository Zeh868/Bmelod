/* SPDX-License-Identifier: GPL-3.0-or-later */
/** @file test_bus_ipc_mode.c @brief BM_BUS_IPC 模式 + native 桩后端契约测试。 */
#include "unity.h"
#include "bm/core/bm_bus.h"
#include "support/ipc_stub_backend.h"
#include "bm/common/bm_types.h"

typedef struct { uint32_t v; } cmd_t;
typedef struct { uint32_t a, b; } tel_t;

void setUp(void) {}
void tearDown(void) {}

/** FIFO：可用容量 cap-1=3，连推 3 成功、第 4 满，FIFO 取回、再取空。 */
void test_ipc_fifo_order_full_empty(void) {
    BM_BUS_DEFINE(b, cmd_t, 4u, 1u, BM_BUS_IPC);
    bm_bus_t h; bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    fifo_stub_t st; fifo_stub_init(&st, sizeof(cmd_t), 4u);
    void *ws; const void *rs;

    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_open(&h, &b_storage, &cfg));
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_bind_ipc_backend(&h, &g_fifo_ipc_iface, &st));

    for (uint32_t i = 0; i < 3u; i++) {
        TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_acquire_write(&h, &ws));
        ((cmd_t *)ws)->v = i + 10u;
        TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_commit(&h));
    }
    TEST_ASSERT_EQUAL_INT(BM_ERR_OVERFLOW, bm_bus_acquire_write(&h, &ws));

    bm_bus_reader_t r; TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_reader_attach(&h, &r));
    for (uint32_t i = 0; i < 3u; i++) {
        TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_acquire_read(&r, &rs));
        TEST_ASSERT_EQUAL_UINT32(i + 10u, ((const cmd_t *)rs)->v);
        TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_release(&r));
    }
    TEST_ASSERT_EQUAL_INT(BM_ERR_WOULD_BLOCK, bm_bus_acquire_read(&r, &rs));
}

/** 未绑定后端：acquire 返回 INVALID。 */
void test_ipc_unbound_invalid(void) {
    BM_BUS_DEFINE(b, cmd_t, 4u, 1u, BM_BUS_IPC);
    bm_bus_t h; bm_bus_cfg_t cfg = { .owner_cpu = 0u }; void *ws;
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_open(&h, &b_storage, &cfg));
    TEST_ASSERT_EQUAL_INT(BM_ERR_INVALID, bm_bus_acquire_write(&h, &ws));
}

/** LATEST：未发布→WOULD_BLOCK；发布后读最新；再发布读到覆盖后的新值。 */
void test_ipc_latest_overwrite(void) {
    BM_BUS_DEFINE(t, tel_t, 3u, 1u, BM_BUS_IPC);
    bm_bus_t h; bm_bus_cfg_t cfg = { .owner_cpu = 1u };
    latest_stub_t st; latest_stub_init(&st, sizeof(tel_t));
    void *ws; const void *rs; bm_bus_reader_t r;

    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_open(&h, &t_storage, &cfg));
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_bind_ipc_backend(&h, &g_latest_ipc_iface, &st));
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_reader_attach(&h, &r));

    TEST_ASSERT_EQUAL_INT(BM_ERR_WOULD_BLOCK, bm_bus_acquire_read(&r, &rs));

    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_acquire_write(&h, &ws));
    ((tel_t *)ws)->a = 1u; ((tel_t *)ws)->b = 2u;
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_commit(&h));
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_acquire_read(&r, &rs));
    TEST_ASSERT_EQUAL_UINT32(1u, ((const tel_t *)rs)->a);
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_release(&r));

    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_acquire_write(&h, &ws));
    ((tel_t *)ws)->a = 9u; ((tel_t *)ws)->b = 8u;
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_commit(&h));
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_acquire_read(&r, &rs));
    TEST_ASSERT_EQUAL_UINT32(9u, ((const tel_t *)rs)->a);
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_release(&r));
}

/** 非 IPC 模式 bind→NOT_SUPPORTED。 */
void test_ipc_bind_wrong_mode(void) {
    BM_BUS_DEFINE(q, tel_t, 4u, 1u, BM_BUS_QUEUE);
    bm_bus_t h; bm_bus_cfg_t cfg = { .owner_cpu = 0u };
    latest_stub_t st; latest_stub_init(&st, sizeof(tel_t));
    TEST_ASSERT_EQUAL_INT(BM_OK, bm_bus_open(&h, &q_storage, &cfg));
    TEST_ASSERT_EQUAL_INT(BM_ERR_NOT_SUPPORTED,
                          bm_bus_bind_ipc_backend(&h, &g_latest_ipc_iface, &st));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_ipc_fifo_order_full_empty);
    RUN_TEST(test_ipc_unbound_invalid);
    RUN_TEST(test_ipc_latest_overwrite);
    RUN_TEST(test_ipc_bind_wrong_mode);
    return UNITY_END();
}
