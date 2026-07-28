/* SPDX-License-Identifier: LGPL-3.0-or-later */
/**
 * @file test_board.c
 * @brief Board 接入契约单元测试
 *
 * 覆盖：注册、能力查询、按类型/名称查找、资源冲突检查、NULL 安全、
 * 重复注册、非法设备类型。
 *
 * 注意：Board 注册为一次性全局状态，本测试通过控制 RUN_TEST 顺序保证
 * 状态隔离——先测试未注册时的行为与所有非法注册，再执行成功注册，
 * 最后验证重复注册。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 Board 接入契约单测
 * 2026-07-28       1.1            zeh            调整 RUN_TEST 顺序，避免
 *                                                一次性注册状态污染失败用例
 * 2026-07-28       1.2            zeh            增加资源数组冲突检查用例
 * 2026-07-28       1.3            zeh            MSG_RAM 改同实例重叠判冲突，
 *                                                补不同实例区间重叠不冲突用例
 */
#include "unity.h"
#include "board/bm_board.h"
#include "board/bm_board_device.h"
#include "bm/common/bm_types.h"

#include <stdint.h>

/** @brief 测试用占位 HAL 设备 */
static const int s_dummy_uart = 1;
static const int s_dummy_spi  = 2;
static const int s_dummy_can  = 3;

void setUp(void) {
}

void tearDown(void) {
}

/* -------------------------------------------------------------------------- */
/*  未注册时的行为与非法注册（必须在任何成功注册之前执行）                         */
/* -------------------------------------------------------------------------- */

static void test_board_before_registration(void) {
    static const bm_board_table_t empty_table = { NULL, 0u, 0u };
    static const bm_board_device_t bad_dev[] = {
        {
            .type = 0xFFu, /* 非法类型 */
            .instance = 0u,
            .name = "bad",
            .hal_dev = &s_dummy_uart,
            .config = NULL,
            .resource_tag = 0u,
        },
    };
    static const bm_board_table_t bad_table = {
        .devices = bad_dev,
        .count = 1u,
        .capabilities = 0u,
    };
    static const bm_board_device_t no_hal[] = {
        {
            .type = BM_BOARD_DEV_TYPE_UART,
            .instance = 0u,
            .name = "no_hal",
            .hal_dev = NULL,
            .config = NULL,
            .resource_tag = 0u,
        },
    };
    static const bm_board_table_t no_hal_table = {
        .devices = no_hal,
        .count = 1u,
        .capabilities = 0u,
    };
    static const bm_board_device_t dup_instance[] = {
        {
            .type = BM_BOARD_DEV_TYPE_UART,
            .instance = 0u,
            .name = "uart0",
            .hal_dev = &s_dummy_uart,
            .config = NULL,
            .resource_tag = 0u,
        },
        {
            .type = BM_BOARD_DEV_TYPE_UART,
            .instance = 0u,
            .name = "uart0b",
            .hal_dev = &s_dummy_spi,
            .config = NULL,
            .resource_tag = 0u,
        },
    };
    static const bm_board_table_t dup_instance_table = {
        .devices = dup_instance,
        .count = 2u,
        .capabilities = 0u,
    };
    static const bm_board_device_t dup_res[] = {
        {
            .type = BM_BOARD_DEV_TYPE_UART,
            .instance = 0u,
            .name = "uart0",
            .hal_dev = &s_dummy_uart,
            .config = NULL,
            .resource_tag = 100u,
        },
        {
            .type = BM_BOARD_DEV_TYPE_CAN,
            .instance = 0u,
            .name = "can0",
            .hal_dev = &s_dummy_can,
            .config = NULL,
            .resource_tag = 100u,
        },
    };
    static const bm_board_table_t dup_res_table = {
        .devices = dup_res,
        .count = 2u,
        .capabilities = 0u,
    };
    static const bm_board_device_t dup_name[] = {
        {
            .type = BM_BOARD_DEV_TYPE_UART,
            .instance = 0u,
            .name = "same_name",
            .hal_dev = &s_dummy_uart,
            .config = NULL,
            .resource_tag = 0u,
        },
        {
            .type = BM_BOARD_DEV_TYPE_SPI,
            .instance = 0u,
            .name = "same_name",
            .hal_dev = &s_dummy_spi,
            .config = NULL,
            .resource_tag = 0u,
        },
    };
    static const bm_board_table_t dup_name_table = {
        .devices = dup_name,
        .count = 2u,
        .capabilities = 0u,
    };
    /* 资源数组冲突：同一 PIN 被 UART 与 SPI 占用 */
    static const bm_board_resource_t pin_res_uart[] = {
        { BM_BOARD_RES_PIN, 1u, 6u, 0u }, /* GPIOB6 */
    };
    static const bm_board_resource_t pin_res_spi[] = {
        { BM_BOARD_RES_PIN, 1u, 6u, 0u }, /* GPIOB6 冲突 */
    };
    static const bm_board_device_t dup_pin[] = {
        {
            .type = BM_BOARD_DEV_TYPE_UART,
            .instance = 0u,
            .name = "uart_pin",
            .hal_dev = &s_dummy_uart,
            .config = NULL,
            .resources = pin_res_uart,
            .resource_count = 1u,
        },
        {
            .type = BM_BOARD_DEV_TYPE_SPI,
            .instance = 0u,
            .name = "spi_pin",
            .hal_dev = &s_dummy_spi,
            .config = NULL,
            .resources = pin_res_spi,
            .resource_count = 1u,
        },
    };
    static const bm_board_table_t dup_pin_table = {
        .devices = dup_pin,
        .count = 2u,
        .capabilities = 0u,
    };
    /* 资源数组冲突：DMA 通道冲突 */
    static const bm_board_resource_t dma_res_a[] = {
        { BM_BOARD_RES_DMA, 1u, 4u, 0u }, /* DMA1_CH4 */
    };
    static const bm_board_resource_t dma_res_b[] = {
        { BM_BOARD_RES_DMA, 1u, 4u, 0u }, /* DMA1_CH4 冲突 */
    };
    static const bm_board_device_t dup_dma[] = {
        {
            .type = BM_BOARD_DEV_TYPE_UART,
            .instance = 1u,
            .name = "uart_dma",
            .hal_dev = &s_dummy_uart,
            .config = NULL,
            .resources = dma_res_a,
            .resource_count = 1u,
        },
        {
            .type = BM_BOARD_DEV_TYPE_SPI,
            .instance = 1u,
            .name = "spi_dma",
            .hal_dev = &s_dummy_spi,
            .config = NULL,
            .resources = dma_res_b,
            .resource_count = 1u,
        },
    };
    static const bm_board_table_t dup_dma_table = {
        .devices = dup_dma,
        .count = 2u,
        .capabilities = 0u,
    };
    /* 资源数组冲突：同一 FDCAN 实例 Message RAM 重叠 */
    static const bm_board_resource_t can1_ram[] = {
        { BM_BOARD_RES_MSG_RAM, 1u, 0u, 128u }, /* FDCAN1: [0, 128) */
    };
    static const bm_board_resource_t can2_ram[] = {
        { BM_BOARD_RES_MSG_RAM, 1u, 64u, 128u }, /* FDCAN1: [64, 192) 与前段重叠 */
    };
    static const bm_board_device_t overlap_can[] = {
        {
            .type = BM_BOARD_DEV_TYPE_CAN,
            .instance = 0u,
            .name = "can1",
            .hal_dev = &s_dummy_can,
            .config = NULL,
            .resources = can1_ram,
            .resource_count = 1u,
        },
        {
            .type = BM_BOARD_DEV_TYPE_CAN,
            .instance = 1u,
            .name = "can2",
            .hal_dev = &s_dummy_can,
            .config = NULL,
            .resources = can2_ram,
            .resource_count = 1u,
        },
    };
    static const bm_board_table_t overlap_can_table = {
        .devices = overlap_can,
        .count = 2u,
        .capabilities = 0u,
    };

    /* 未注册时的查询行为 */
    TEST_ASSERT_EQUAL(0u, bm_board_device_count());
    TEST_ASSERT_EQUAL(0u, bm_board_capability_mask());
    TEST_ASSERT(!bm_board_has_capability(BM_CAP_UART));
    TEST_ASSERT_NULL(bm_board_find(BM_BOARD_DEV_TYPE_UART, 0u));
    TEST_ASSERT_NULL(bm_board_find_by_name("anything"));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_board_check_conflicts());

    /* 非法注册 */
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_board_register(NULL));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_board_register(&empty_table));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_board_register(&bad_table));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_board_register(&no_hal_table));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_board_register(&dup_instance_table));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_board_register(&dup_res_table));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_board_register(&dup_name_table));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_board_register(&dup_pin_table));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_board_register(&dup_dma_table));
    TEST_ASSERT_EQUAL(BM_ERR_INVALID, bm_board_register(&overlap_can_table));
}

/* -------------------------------------------------------------------------- */
/*  正常注册与查询                                                              */
/* -------------------------------------------------------------------------- */

static void test_board_register_and_find(void) {
    static const bm_board_resource_t uart_res[] = {
        { BM_BOARD_RES_PIN, 1u, 10u, 0u }, /* GPIOB10 */
        { BM_BOARD_RES_DMA, 1u, 4u, 0u },  /* DMA1_CH4 */
        { BM_BOARD_RES_IRQ, 0u, 39u, 0u }, /* USART3_IRQn ≈ 39 */
        { BM_BOARD_RES_MSG_RAM, 1u, 0u, 128u }, /* FDCAN1: [0, 128) */
    };
    static const bm_board_resource_t spi_res[] = {
        { BM_BOARD_RES_PIN, 1u, 13u, 0u }, /* GPIOB13 */
        { BM_BOARD_RES_DMA, 1u, 5u, 0u },  /* DMA1_CH5 */
        { BM_BOARD_RES_IRQ, 0u, 35u, 0u }, /* SPI1_IRQn ≈ 35 */
        /* FDCAN2: [64, 192) 区间与 FDCAN1 重叠，但实例不同不冲突 */
        { BM_BOARD_RES_MSG_RAM, 2u, 64u, 128u },
    };
    static const bm_board_device_t devices[] = {
        {
            .type = BM_BOARD_DEV_TYPE_UART,
            .instance = 0u,
            .name = "uart_console",
            .hal_dev = &s_dummy_uart,
            .config = NULL,
            .resources = uart_res,
            .resource_count = 4u,
            .resource_tag = 0u,
        },
        {
            .type = BM_BOARD_DEV_TYPE_SPI,
            .instance = 0u,
            .name = "spi_flash",
            .hal_dev = &s_dummy_spi,
            .config = NULL,
            .resources = spi_res,
            .resource_count = 4u,
            .resource_tag = 0u,
        },
    };
    static const bm_board_table_t table = {
        .devices = devices,
        .count = 2u,
        .capabilities = BM_CAP_DMA | BM_CAP_IRQ,
    };
    const bm_board_device_t *found;

    TEST_ASSERT_EQUAL(BM_OK, bm_board_register(&table));
    TEST_ASSERT_EQUAL(2u, bm_board_device_count());

    found = bm_board_find(BM_BOARD_DEV_TYPE_UART, 0u);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL(BM_BOARD_DEV_TYPE_UART, found->type);
    TEST_ASSERT_EQUAL(0u, found->instance);
    TEST_ASSERT_EQUAL_PTR(&s_dummy_uart, found->hal_dev);

    found = bm_board_find(BM_BOARD_DEV_TYPE_CAN, 0u);
    TEST_ASSERT_NULL(found);

    found = bm_board_find_by_name("spi_flash");
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL(BM_BOARD_DEV_TYPE_SPI, found->type);

    found = bm_board_find_by_name("nonexistent");
    TEST_ASSERT_NULL(found);

    TEST_ASSERT(bm_board_has_capability(BM_CAP_UART));
    TEST_ASSERT(bm_board_has_capability(BM_CAP_SPI));
    TEST_ASSERT(bm_board_has_capability(BM_CAP_DMA));
    TEST_ASSERT(bm_board_has_capability(BM_CAP_IRQ));
    TEST_ASSERT(!bm_board_has_capability(BM_CAP_CAN));
    TEST_ASSERT(!bm_board_has_capability(0u)); /* 空能力始终返回 0 */

    TEST_ASSERT_EQUAL(BM_CAP_UART | BM_CAP_SPI | BM_CAP_DMA | BM_CAP_IRQ,
                      bm_board_capability_mask());

    TEST_ASSERT_EQUAL(BM_OK, bm_board_check_conflicts());
}

/* -------------------------------------------------------------------------- */
/*  重复注册                                                                    */
/* -------------------------------------------------------------------------- */

static void test_board_register_already(void) {
    static const bm_board_table_t table = { NULL, 0u, 0u };

    /* 此时 board 已由上一个用例注册成功 */
    TEST_ASSERT_EQUAL(BM_ERR_ALREADY, bm_board_register(&table));
}

static void test_board_init_devices_already(void) {
    static const bm_board_device_t dev[] = {
        {
            .type = BM_BOARD_DEV_TYPE_UART,
            .instance = 1u,
            .name = "uart_extra",
            .hal_dev = &s_dummy_uart,
            .config = NULL,
        },
    };

    /* board 已注册，init_devices 也应返回 ALREADY */
    TEST_ASSERT_EQUAL(BM_ERR_ALREADY,
                      bm_board_init_devices(dev, 1u, 0u));
}

/* -------------------------------------------------------------------------- */
/*  主函数                                                                      */
/* -------------------------------------------------------------------------- */

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_board_before_registration);
    RUN_TEST(test_board_register_and_find);
    RUN_TEST(test_board_register_already);
    RUN_TEST(test_board_init_devices_already);
    return UNITY_END();
}
