/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_can_stm32g4.c
 * @brief STM32G4 FDCAN1/FDCAN2 后端（寄存器级直接操作，等效 LL 风格）
 *
 * STM32CubeG4 未提供 FDCAN LL 头文件，因此本后端基于 CMSIS 寄存器定义直接操作，
 * 仅依赖 `stm32g4xx.h`、`stm32g4xx_ll_bus.h`/`ll_gpio.h`/`ll_rcc.h` 做时钟/GPIO。
 *
 * 重要硬件约束（STM32G4 参考手册 RM0440）：
 * - FDCAN Message RAM 布局由硬件固定，每个实例 212 words（848 bytes），不可通过
 *   SIDFC/XIDFC/RXF0C/RXF1C/RXESC/TXESC/TXEFC 寄存器配置（这些寄存器在 G4 上不存在）。
 * - 每个实例固定：28 个标准过滤器、8 个扩展过滤器、RX FIFO0/1 各 3 元素、
 *   TX Event FIFO 3 元素、TX FIFO/Queue 3 元素；RX/TX 元素固定 64 字节。
 * - `bm_can_stm32g4_config_t` 中的 `*_count` 字段仅决定软件实际使用数量，不能超过
 *   上述硬件上限。Message RAM 起始偏移由 `fdcan` 实例固定推导：FDCAN1=0、
 *   FDCAN2=`BM_CAN_G4_SIZE`(212)，对齐 ST `SRAMCAN_BASE + SRAMCAN_SIZE`；
 *   配置字段 `message_ram_offset` 被忽略，Board 只选实例不计算偏移。
 * - 中断仅支持 Line 0（`FDCANx_IT0_IRQn`）；ILS 固定为 0，无 IT1 Handler。
 *
 * 本后端支持：
 * - FDCAN1/FDCAN2 多实例，引脚/AF/IRQ 由 App 配置。
 * - 标准帧与扩展帧硬件过滤器。
 * - RX FIFO0 有界循环处理、FIFO 满/消息丢失检测、TX FIFO 满检测。
 * - Bus-off / error warning / error passive 检测与手动恢复。
 * - 真实接收时间戳（`bm_uptime_us()`）。
 *
 * 应用通过 `bm_can_stm32g4_config_t` 指定 FDCANx/引脚/位时序/IRQ；
 * Bmelod 不固定 FDCAN 编号与产品引脚。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 STM32G4 FDCAN 寄存器级后端
 * 2026-07-28       1.1            zeh            使用 message_ram_offset、扩展过滤器、
 *                                                真实时间戳、bus-off 恢复
 * 2026-07-28       1.2            zeh            补 RX FIFO1 ISR 分支（修中断风暴）；
 *                                                删除 G4 不存在的 TXBC.TFQS 写入；
 *                                                fd_enabled 同置 BRSE；ctx_for 改按
 *                                                dev 匹配；TX Message Marker 移至
 *                                                T1[31:24]；validate 只接受 IT0 向量；
 *                                                recover 不再强制清 bus_off 软件标志
 * 2026-07-28       1.3            zeh            Message RAM 偏移按实例强制 0/212；
 *                                                忽略 App 可配 message_ram_offset
 */
#include "bm_vendor_can_stm32g4.h"
#include "bm_hal_instances_stm32g4.h"
#include "bm/common/bm_types.h"
#include "bm/common/bm_uptime.h"
#include "drv/bm_drv_can.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "stm32g4xx.h"
#include "stm32g4xx_ll_bus.h"
#include "stm32g4xx_ll_gpio.h"
#include "stm32g4xx_ll_rcc.h"

/** @brief 最大支持的 FDCAN 实例数。 */
#define BM_CAN_STM32G4_INSTANCE_COUNT 2u

/** @brief STM32G4 全局 FDCAN Message RAM 基址（10KB，2560 words）。 */
#define BM_CAN_STM32G4_MSG_RAM_BASE 0x4000A400u

/** @brief 全局 Message RAM 总容量（words）。 */
#define BM_CAN_STM32G4_MSG_RAM_WORDS 2560u

/* ---------- STM32G4 固定 Message RAM 布局（RM0440 / HAL 一致） ---------- */

#define BM_CAN_G4_FLS_NBR 28u   /**< 标准过滤器最大数。 */
#define BM_CAN_G4_FLE_NBR 8u    /**< 扩展过滤器最大数。 */
#define BM_CAN_G4_RF0_NBR 3u    /**< RX FIFO0 元素最大数。 */
#define BM_CAN_G4_RF1_NBR 3u    /**< RX FIFO1 元素最大数。 */
#define BM_CAN_G4_TEF_NBR 3u    /**< TX Event FIFO 元素最大数。 */
#define BM_CAN_G4_TFQ_NBR 3u    /**< TX FIFO/Queue 元素最大数。 */

#define BM_CAN_G4_FLS_SIZE 1u   /**< words */
#define BM_CAN_G4_FLE_SIZE 2u
#define BM_CAN_G4_RF0_SIZE 18u
#define BM_CAN_G4_RF1_SIZE 18u
#define BM_CAN_G4_TEF_SIZE 2u
#define BM_CAN_G4_TFQ_SIZE 18u

#define BM_CAN_G4_FLSSA 0u
#define BM_CAN_G4_FLESA (BM_CAN_G4_FLSSA + (BM_CAN_G4_FLS_NBR * BM_CAN_G4_FLS_SIZE))
#define BM_CAN_G4_RF0SA (BM_CAN_G4_FLESA + (BM_CAN_G4_FLE_NBR * BM_CAN_G4_FLE_SIZE))
#define BM_CAN_G4_RF1SA (BM_CAN_G4_RF0SA + (BM_CAN_G4_RF0_NBR * BM_CAN_G4_RF0_SIZE))
#define BM_CAN_G4_TEFSA (BM_CAN_G4_RF1SA + (BM_CAN_G4_RF1_NBR * BM_CAN_G4_RF1_SIZE))
#define BM_CAN_G4_TFQSA (BM_CAN_G4_TEFSA + (BM_CAN_G4_TEF_NBR * BM_CAN_G4_TEF_SIZE))
#define BM_CAN_G4_SIZE  (BM_CAN_G4_TFQSA + (BM_CAN_G4_TFQ_NBR * BM_CAN_G4_TFQ_SIZE)) /**< 212 words */

/**
 * @brief 按 FDCAN 实例返回固定 Message RAM word 偏移（忽略配置字段）。
 *
 * 对齐 ST 官方布局：FDCAN1 @ 0，FDCAN2 @ SRAMCAN_SIZE(=212)。
 */
static uint32_t bm_can_stm32g4_fixed_msg_ram_offset(
    const bm_can_stm32g4_config_t *cfg) {
    if (cfg != NULL && cfg->fdcan == FDCAN2) {
        return BM_CAN_G4_SIZE;
    }
    return 0u;
}

/* ---------- 寄存器位定义（CMSIS 已定义大部分，这里只补充缺失的） ---------- */

/* CCCR */
#ifndef FDCAN_CCCR_INIT
#define FDCAN_CCCR_INIT  (1u << 0u)
#endif
#ifndef FDCAN_CCCR_CCE
#define FDCAN_CCCR_CCE   (1u << 1u)
#endif
#ifndef FDCAN_CCCR_DAR
#define FDCAN_CCCR_DAR   (1u << 6u)
#endif
#ifndef FDCAN_CCCR_FDOE
#define FDCAN_CCCR_FDOE  (1u << 8u)
#endif
#ifndef FDCAN_CCCR_BRSE
#define FDCAN_CCCR_BRSE  (1u << 9u)
#endif

/* NBTP */
#ifndef FDCAN_NBTP_NTSEG2_Pos
#define FDCAN_NBTP_NTSEG2_Pos 0u
#endif
#ifndef FDCAN_NBTP_NTSEG1_Pos
#define FDCAN_NBTP_NTSEG1_Pos 8u
#endif
#ifndef FDCAN_NBTP_NBRP_Pos
#define FDCAN_NBTP_NBRP_Pos   16u
#endif
#ifndef FDCAN_NBTP_NSJW_Pos
#define FDCAN_NBTP_NSJW_Pos   25u
#endif

/* DBTP */
#ifndef FDCAN_DBTP_DSJW_Pos
#define FDCAN_DBTP_DSJW_Pos   0u
#endif
#ifndef FDCAN_DBTP_DTSEG2_Pos
#define FDCAN_DBTP_DTSEG2_Pos 4u
#endif
#ifndef FDCAN_DBTP_DTSEG1_Pos
#define FDCAN_DBTP_DTSEG1_Pos 8u
#endif
#ifndef FDCAN_DBTP_DBRP_Pos
#define FDCAN_DBTP_DBRP_Pos   16u
#endif

/* TXBC：STM32G4 只有 TFQM（bit24，FIFO/Queue 模式），无 TFQS 字段
 *（TFQS bit30 是 H5 定义；G4 的 TX FIFO 元素数由硬件固定，见
 * stm32g474xx.h:4985），此处不再补定义 */

/* RXGFC */
#ifndef FDCAN_RXGFC_RRFE
#define FDCAN_RXGFC_RRFE (1u << 0u)
#endif
#ifndef FDCAN_RXGFC_RRFS
#define FDCAN_RXGFC_RRFS (1u << 1u)
#endif
#ifndef FDCAN_RXGFC_ANFE_Pos
#define FDCAN_RXGFC_ANFE_Pos 2u
#endif
#ifndef FDCAN_RXGFC_ANFS_Pos
#define FDCAN_RXGFC_ANFS_Pos 4u
#endif
#ifndef FDCAN_RXGFC_LSS_Pos
#define FDCAN_RXGFC_LSS_Pos 16u
#endif
#ifndef FDCAN_RXGFC_LSE_Pos
#define FDCAN_RXGFC_LSE_Pos 24u
#endif

/* IR / IE */
#ifndef FDCAN_IR_RF0N
#define FDCAN_IR_RF0N  (1u << 0u)
#endif
#ifndef FDCAN_IR_RF0F
#define FDCAN_IR_RF0F  (1u << 1u)
#endif
#ifndef FDCAN_IR_RF0L
#define FDCAN_IR_RF0L  (1u << 2u)
#endif
#ifndef FDCAN_IR_RF1N
#define FDCAN_IR_RF1N  (1u << 3u)
#endif
#ifndef FDCAN_IR_RF1F
#define FDCAN_IR_RF1F  (1u << 4u)
#endif
#ifndef FDCAN_IR_RF1L
#define FDCAN_IR_RF1L  (1u << 5u)
#endif
#ifndef FDCAN_IR_TC
#define FDCAN_IR_TC    (1u << 7u)
#endif
#ifndef FDCAN_IR_TEFL
#define FDCAN_IR_TEFL  (1u << 12u)
#endif
#ifndef FDCAN_IR_EW
#define FDCAN_IR_EW    (1u << 13u)
#endif
#ifndef FDCAN_IR_EP
#define FDCAN_IR_EP    (1u << 14u)
#endif
#ifndef FDCAN_IR_BO
#define FDCAN_IR_BO    (1u << 15u)
#endif

/* ILE */
#ifndef FDCAN_ILE_EINT0
#define FDCAN_ILE_EINT0 (1u << 0u)
#endif

/* PSR */
#ifndef FDCAN_PSR_EP
#define FDCAN_PSR_EP (1u << 5u)
#endif
#ifndef FDCAN_PSR_EW
#define FDCAN_PSR_EW (1u << 6u)
#endif
#ifndef FDCAN_PSR_BO
#define FDCAN_PSR_BO (1u << 7u)
#endif

/* Message RAM element masks */
#ifndef FDCAN_ELEMENT_MASK_XTD
#define FDCAN_ELEMENT_MASK_XTD   (1u << 30u)
#endif
#ifndef FDCAN_ELEMENT_MASK_STDID
#define FDCAN_ELEMENT_MASK_STDID (0x7FFu << 18u)
#endif
#ifndef FDCAN_ELEMENT_MASK_EXTID
#define FDCAN_ELEMENT_MASK_EXTID (0x1FFFFFFFu)
#endif
#ifndef FDCAN_ELEMENT_MASK_RTR
#define FDCAN_ELEMENT_MASK_RTR   (1u << 29u)
#endif
#ifndef FDCAN_ELEMENT_MASK_DLC
#define FDCAN_ELEMENT_MASK_DLC   (0xFu << 16u)
#endif
#ifndef FDCAN_ELEMENT_MASK_BRS
#define FDCAN_ELEMENT_MASK_BRS   (1u << 20u)
#endif
#ifndef FDCAN_ELEMENT_MASK_FDF
#define FDCAN_ELEMENT_MASK_FDF   (1u << 21u)
#endif

/* Standard filter element word layout */
#define FDCAN_SFEC_DISABLE    0u
#define FDCAN_SFEC_FIFO0      1u
#define FDCAN_SFEC_FIFO1      2u
#define FDCAN_SFEC_REJECT     3u

#define FDCAN_SFT_RANGE       0u
#define FDCAN_SFT_DUAL        1u
#define FDCAN_SFT_MASK        2u

/* Extended filter element word layout */
#define FDCAN_EFEC_DISABLE    0u
#define FDCAN_EFEC_FIFO0      1u
#define FDCAN_EFEC_FIFO1      2u
#define FDCAN_EFEC_REJECT     3u

#define FDCAN_EFT_RANGE       0u
#define FDCAN_EFT_DUAL        1u
#define FDCAN_EFT_MASK        2u

/* -------------------------------------------------------------------------- */
/*  运行时上下文                                                                */
/* -------------------------------------------------------------------------- */

typedef struct {
    const struct bm_hal_can       *dev;
    const bm_can_stm32g4_config_t *cfg;
    int                            initialized;
    int                            started;
    bm_can_rx_callback_t           rx_cb;
    void                          *rx_user;
    bm_can_event_callback_t        event_cb;
    void                          *event_user;
    bm_can_stats_t                 stats;
    int                            bus_off;
    uint32_t                       tx_put_index;
} bm_can_stm32g4_context_t;

static bm_can_stm32g4_context_t g_can_ctx[BM_CAN_STM32G4_INSTANCE_COUNT];

/* -------------------------------------------------------------------------- */
/*  默认配置                                                                    */
/* -------------------------------------------------------------------------- */

/** @brief 默认 FDCAN1 配置（PB8/PB9，AF9，500k Classic CAN）。 */
static const bm_can_stm32g4_config_t g_can_cfg_1 = {
    .fdcan = FDCAN1,
    .rcc_apb1 = LL_APB1_GRP1_PERIPH_FDCAN,
    .tx_pin = BM_STM32G4_CAN1_TX_PIN,
    .rx_pin = BM_STM32G4_CAN1_RX_PIN,
    .gpio_af = BM_STM32G4_CAN_GPIO_AF,
    .nbtr = BM_STM32G4_CAN_NBTR,
    .dbtr = { .prescaler = 8u, .tseg1 = 7u, .tseg2 = 2u, .sjw = 1u },
    .fd_enabled = 0u,
    .message_ram_offset = 0u, /* 忽略；后端按 FDCAN1 强制 0 */
    .std_filter_count = 8u,
    .ext_filter_count = 0u,
    .rx_fifo0_count = BM_CAN_G4_RF0_NBR,
    .rx_fifo1_count = 0u,
    .tx_fifo_count = BM_CAN_G4_TFQ_NBR,
    .tx_event_fifo_count = 0u,
    .rx_elmt_size = 7u, /* 64 bytes（硬件固定） */
    .tx_elmt_size = 7u,
    .irqn = FDCAN1_IT0_IRQn,
    .irq_priority = BM_STM32G4_CAN_IRQ_PRIORITY,
};

/** @brief 默认 FDCAN2 配置（PB12/PB13，AF9，500k Classic CAN）。 */
static const bm_can_stm32g4_config_t g_can_cfg_2 = {
    .fdcan = FDCAN2,
    .rcc_apb1 = LL_APB1_GRP1_PERIPH_FDCAN,
    .tx_pin = BM_STM32G4_CAN2_TX_PIN,
    .rx_pin = BM_STM32G4_CAN2_RX_PIN,
    .gpio_af = BM_STM32G4_CAN_GPIO_AF,
    .nbtr = BM_STM32G4_CAN_NBTR,
    .dbtr = { .prescaler = 8u, .tseg1 = 7u, .tseg2 = 2u, .sjw = 1u },
    .fd_enabled = 0u,
    .message_ram_offset = 0u, /* 忽略；后端按 FDCAN2 强制 212 */
    .std_filter_count = 8u,
    .ext_filter_count = 0u,
    .rx_fifo0_count = BM_CAN_G4_RF0_NBR,
    .rx_fifo1_count = 0u,
    .tx_fifo_count = BM_CAN_G4_TFQ_NBR,
    .tx_event_fifo_count = 0u,
    .rx_elmt_size = 7u,
    .tx_elmt_size = 7u,
    .irqn = FDCAN2_IT0_IRQn,
    .irq_priority = BM_STM32G4_CAN_IRQ_PRIORITY,
};

/* -------------------------------------------------------------------------- */
/*  DLC 与字节数互转                                                            */
/* -------------------------------------------------------------------------- */

static const uint8_t bm_can_dlc_to_bytes[16] = {
    0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u,
    12u, 16u, 20u, 24u, 32u, 48u, 64u
};

static uint8_t bm_can_bytes_to_dlc(uint8_t len) {
    if (len <= 8u) {
        return len;
    }
    if (len <= 12u) return 9u;
    if (len <= 16u) return 10u;
    if (len <= 20u) return 11u;
    if (len <= 24u) return 12u;
    if (len <= 32u) return 13u;
    if (len <= 48u) return 14u;
    return 15u;
}

/* -------------------------------------------------------------------------- */
/*  底层辅助                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief 由设备实例获取运行时上下文。
 */
static bm_can_stm32g4_context_t *bm_can_stm32g4_ctx_for(
    const struct bm_hal_can *dev) {
    bm_can_stm32g4_context_t *free_slot = NULL;
    uint32_t i;

    if (dev == NULL) {
        return NULL;
    }
    /* 按 dev 指针匹配：init 允许用 config 入参覆盖 dev->config，
     * 按 cfg 匹配会在覆盖场景失配 */
    for (i = 0u; i < BM_CAN_STM32G4_INSTANCE_COUNT; ++i) {
        if (g_can_ctx[i].dev == dev) {
            return &g_can_ctx[i];
        }
        if (free_slot == NULL && g_can_ctx[i].cfg == NULL) {
            free_slot = &g_can_ctx[i];
        }
    }
    return free_slot;
}

/**
 * @brief 由 FDCAN 寄存器基址获取上下文。
 */
static bm_can_stm32g4_context_t *bm_can_stm32g4_ctx_for_fdcan(
    FDCAN_GlobalTypeDef *fdcan) {
    uint32_t i;

    for (i = 0u; i < BM_CAN_STM32G4_INSTANCE_COUNT; ++i) {
        if (g_can_ctx[i].cfg != NULL && g_can_ctx[i].cfg->fdcan == fdcan) {
            return &g_can_ctx[i];
        }
    }
    return NULL;
}

/**
 * @brief 计算引脚所在 GPIO 端口与位掩码。
 */
static GPIO_TypeDef *bm_can_stm32g4_port(uint32_t pin_encoding, uint32_t *pin_num) {
    uint32_t port = pin_encoding >> 4u;
    *pin_num = pin_encoding & 0x0Fu;

    switch (port) {
    case 0u: return GPIOA;
    case 1u: return GPIOB;
    case 2u: return GPIOC;
    case 3u: return GPIOD;
    case 4u: return GPIOE;
    case 5u: return GPIOF;
    case 6u: return GPIOG;
    default: return NULL;
    }
}

/**
 * @brief 配置 CAN TX/RX GPIO 复用。
 */
static int bm_can_stm32g4_gpio_init(const bm_can_stm32g4_config_t *cfg) {
    GPIO_TypeDef *tx_port, *rx_port;
    uint32_t tx_pin, rx_pin;
    uint32_t tx_mask, rx_mask;

    tx_port = bm_can_stm32g4_port(cfg->tx_pin, &tx_pin);
    rx_port = bm_can_stm32g4_port(cfg->rx_pin, &rx_pin);
    if (tx_port == NULL || rx_port == NULL) {
        return BM_ERR_INVALID;
    }

    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_GPIOA |
                             LL_AHB2_GRP1_PERIPH_GPIOB |
                             LL_AHB2_GRP1_PERIPH_GPIOC |
                             LL_AHB2_GRP1_PERIPH_GPIOD);

    tx_mask = 1u << tx_pin;
    rx_mask = 1u << rx_pin;

    LL_GPIO_SetPinMode(tx_port, tx_mask, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetPinSpeed(tx_port, tx_mask, LL_GPIO_SPEED_FREQ_HIGH);
    LL_GPIO_SetPinOutputType(tx_port, tx_mask, LL_GPIO_OUTPUT_PUSHPULL);
    LL_GPIO_SetPinPull(tx_port, tx_mask, LL_GPIO_PULL_NO);

    LL_GPIO_SetPinMode(rx_port, rx_mask, LL_GPIO_MODE_ALTERNATE);
    LL_GPIO_SetPinSpeed(rx_port, rx_mask, LL_GPIO_SPEED_FREQ_HIGH);
    LL_GPIO_SetPinPull(rx_port, rx_mask, LL_GPIO_PULL_NO);

    if (tx_pin < 8u) {
        LL_GPIO_SetAFPin_0_7(tx_port, tx_mask, cfg->gpio_af);
    } else {
        LL_GPIO_SetAFPin_8_15(tx_port, tx_mask, cfg->gpio_af);
    }
    if (rx_pin < 8u) {
        LL_GPIO_SetAFPin_0_7(rx_port, rx_mask, cfg->gpio_af);
    } else {
        LL_GPIO_SetAFPin_8_15(rx_port, rx_mask, cfg->gpio_af);
    }

    return BM_OK;
}

/**
 * @brief 等待 CCCR.INIT 达到目标状态，超时返回错误。
 */
static int bm_can_stm32g4_wait_init(FDCAN_GlobalTypeDef *fdcan, uint32_t target) {
    uint32_t timeout = 100000u;

    while (((fdcan->CCCR & FDCAN_CCCR_INIT) != target) && (timeout != 0u)) {
        timeout--;
    }
    return ((fdcan->CCCR & FDCAN_CCCR_INIT) == target) ? BM_OK : BM_ERR_TIMEOUT;
}

/**
 * @brief 获取 Message RAM 中某实例区域的绝对地址。
 *
 * @param cfg     FDCAN 平台配置（偏移由 fdcan 实例固定推导）
 * @param offset  实例内 32-bit word 偏移
 */
static volatile uint32_t *bm_can_stm32g4_msg_ram(
    const bm_can_stm32g4_config_t *cfg, uint32_t offset) {
    uint32_t base_off = bm_can_stm32g4_fixed_msg_ram_offset(cfg);
    uint32_t addr = BM_CAN_STM32G4_MSG_RAM_BASE +
                    (base_off + offset) * 4u;

    return (volatile uint32_t *)addr;
}

/**
 * @brief 进入 INIT 模式并解锁配置（CCE=1）。
 */
static int bm_can_stm32g4_enter_init(FDCAN_GlobalTypeDef *fdcan) {
    int rc;

    fdcan->CCCR |= FDCAN_CCCR_INIT;
    rc = bm_can_stm32g4_wait_init(fdcan, FDCAN_CCCR_INIT);
    if (rc != BM_OK) {
        return rc;
    }
    fdcan->CCCR |= FDCAN_CCCR_CCE;
    return BM_OK;
}

/**
 * @brief 退出 INIT 模式。
 */
static int bm_can_stm32g4_leave_init(FDCAN_GlobalTypeDef *fdcan) {
    fdcan->CCCR &= ~(FDCAN_CCCR_INIT | FDCAN_CCCR_CCE);
    return bm_can_stm32g4_wait_init(fdcan, 0u);
}

/* -------------------------------------------------------------------------- */
/*  配置校验                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief 校验位时序参数范围。
 */
static int bm_can_stm32g4_validate_timing(const bm_can_stm32g4_bit_timing_t *bt) {
    if (bt->prescaler < 1u || bt->prescaler > 512u ||
        bt->tseg1 < 1u || bt->tseg1 > 256u ||
        bt->tseg2 < 1u || bt->tseg2 > 128u ||
        bt->sjw < 1u || bt->sjw > 128u) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

/**
 * @brief 校验整个 FDCAN 配置。
 */
static int bm_can_stm32g4_validate_config(const bm_can_stm32g4_config_t *cfg) {
    if (cfg == NULL) {
        return BM_ERR_INVALID;
    }
    if (cfg->fdcan != FDCAN1 && cfg->fdcan != FDCAN2) {
        return BM_ERR_INVALID;
    }
    if (cfg->gpio_af > 15u) {
        return BM_ERR_INVALID;
    }
    if (bm_can_stm32g4_port(cfg->tx_pin, &(uint32_t){0u}) == NULL ||
        bm_can_stm32g4_port(cfg->rx_pin, &(uint32_t){0u}) == NULL) {
        return BM_ERR_INVALID;
    }
    /* 只接受 IT0 向量：ILS=0 将全部中断映射到 Line 0，且本后端只定义
     * FDCANx_IT0_IRQHandler；传 IT1 向量会导致中断永远无人处理 */
    if (cfg->irqn != FDCAN1_IT0_IRQn && cfg->irqn != FDCAN2_IT0_IRQn) {
        return BM_ERR_INVALID;
    }
    if (bm_can_stm32g4_validate_timing(&cfg->nbtr) != BM_OK) {
        return BM_ERR_INVALID;
    }
    if (cfg->fd_enabled != 0u) {
        if (bm_can_stm32g4_validate_timing(&cfg->dbtr) != BM_OK) {
            return BM_ERR_INVALID;
        }
    }
    if (cfg->std_filter_count > BM_CAN_G4_FLS_NBR ||
        cfg->ext_filter_count > BM_CAN_G4_FLE_NBR ||
        cfg->rx_fifo0_count > BM_CAN_G4_RF0_NBR ||
        cfg->rx_fifo1_count > BM_CAN_G4_RF1_NBR ||
        cfg->tx_fifo_count > BM_CAN_G4_TFQ_NBR ||
        cfg->tx_event_fifo_count > BM_CAN_G4_TEF_NBR) {
        return BM_ERR_INVALID;
    }
    /* G4 硬件固定 64 字节元素；rx_elmt_size/tx_elmt_size 保留字段，只接受 7 */
    if (cfg->rx_elmt_size != 7u || cfg->tx_elmt_size != 7u) {
        return BM_ERR_INVALID;
    }
    /* message_ram_offset 字段忽略；边界按固定 0/212 校验 */
    {
        uint32_t ram_off = bm_can_stm32g4_fixed_msg_ram_offset(cfg);

        if ((ram_off + BM_CAN_G4_SIZE) > BM_CAN_STM32G4_MSG_RAM_WORDS) {
            return BM_ERR_INVALID;
        }
    }

    return BM_OK;
}

/**
 * @brief 计算并返回实际波特率（bps）与采样点（千分比）。
 */
static void bm_can_stm32g4_calc_bitrate(const bm_can_stm32g4_config_t *cfg,
                                        uint32_t *bitrate, uint32_t *sample_pt_promille) {
    LL_RCC_ClocksTypeDef clocks;
    uint32_t pclk;
    uint32_t tq;
    uint32_t sp;

    LL_RCC_GetSystemClocksFreq(&clocks);
    pclk = clocks.PCLK1_Frequency;

    tq = cfg->nbtr.prescaler * (cfg->nbtr.tseg1 + cfg->nbtr.tseg2 + 1u);
    if (tq == 0u) {
        *bitrate = 0u;
        *sample_pt_promille = 0u;
        return;
    }
    *bitrate = pclk / tq;

    sp = ((cfg->nbtr.tseg1 + 1u) * 1000u) /
         (cfg->nbtr.tseg1 + cfg->nbtr.tseg2 + 1u);
    *sample_pt_promille = sp;
}

/* -------------------------------------------------------------------------- */
/*  硬件初始化                                                                  */
/* -------------------------------------------------------------------------- */

static int bm_can_stm32g4_hw_init(bm_can_stm32g4_context_t *ctx) {
    const bm_can_stm32g4_config_t *cfg = ctx->cfg;
    FDCAN_GlobalTypeDef *fdcan = cfg->fdcan;
    uint32_t nbtp, dbtp;
    uint32_t i;
    int rc;

    rc = bm_can_stm32g4_validate_config(cfg);
    if (rc != BM_OK) {
        return rc;
    }

    /* 使能时钟（FDCAN1/FDCAN2 共享 APB1 的 FDCANEN 位） */
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_FDCAN);

    /* 配置 GPIO */
    rc = bm_can_stm32g4_gpio_init(cfg);
    if (rc != BM_OK) {
        return rc;
    }

    /* 进入 INIT + CCE */
    if (bm_can_stm32g4_enter_init(fdcan) != BM_OK) {
        return BM_ERR_TIMEOUT;
    }

    /* 全局配置 */
    fdcan->CCCR = FDCAN_CCCR_INIT | FDCAN_CCCR_CCE | FDCAN_CCCR_DAR;
    if (cfg->fd_enabled != 0u) {
        /* FDOE 使能 CAN FD，BRSE 使能数据段速率切换；
         * 只置 FDOE 会导致带 BRS 标志的帧静默以标称速率发送 */
        fdcan->CCCR |= FDCAN_CCCR_FDOE | FDCAN_CCCR_BRSE;
    }

    /* 位时序 */
    nbtp = ((cfg->nbtr.tseg2 - 1u) << FDCAN_NBTP_NTSEG2_Pos) |
           ((cfg->nbtr.tseg1 - 1u) << FDCAN_NBTP_NTSEG1_Pos) |
           ((cfg->nbtr.prescaler - 1u) << FDCAN_NBTP_NBRP_Pos) |
           ((cfg->nbtr.sjw - 1u) << FDCAN_NBTP_NSJW_Pos);
    fdcan->NBTP = nbtp;

    if (cfg->fd_enabled != 0u) {
        dbtp = ((cfg->dbtr.tseg2 - 1u) << FDCAN_DBTP_DTSEG2_Pos) |
               ((cfg->dbtr.tseg1 - 1u) << FDCAN_DBTP_DTSEG1_Pos) |
               ((cfg->dbtr.prescaler - 1u) << FDCAN_DBTP_DBRP_Pos) |
               ((cfg->dbtr.sjw - 1u) << FDCAN_DBTP_DSJW_Pos);
        fdcan->DBTP = dbtp;
    }

    /* 清零本实例 Message RAM */
    for (i = 0u; i < BM_CAN_G4_SIZE; ++i) {
        bm_can_stm32g4_msg_ram(cfg, i)[0] = 0u;
    }

    /* 全局过滤器：默认拒绝所有非匹配帧（fail-closed），应用通过 add_filter 开通。
     * STM32G4 无 SIDFC/XIDFC/RXF0C/RXF1C/RXESC/TXESC/TXEFC 寄存器，
     * 过滤器/RX/TX 数量与元素大小由硬件固定，仅通过 RXGFC 的 LSS/LSE 控制实际使用数。 */
    fdcan->RXGFC = FDCAN_RXGFC_RRFE | FDCAN_RXGFC_RRFS |
                   (2u << FDCAN_RXGFC_ANFE_Pos) |
                   (2u << FDCAN_RXGFC_ANFS_Pos) |
                   ((cfg->std_filter_count & 0xFFu) << FDCAN_RXGFC_LSS_Pos) |
                   ((cfg->ext_filter_count & 0xFFu) << FDCAN_RXGFC_LSE_Pos);

    /* TX FIFO 模式（TFQM=0）。G4 的 TXBC 无 TFQS 字段，TX FIFO 元素数
     * 由硬件固定为 3，cfg->tx_fifo_count 仅决定软件使用数量（validate 已限上界） */
    fdcan->TXBC = 0u;

    /* 中断：使能 RX FIFO0 新消息、TX 完成、bus-off、error warning/passive */
    fdcan->IE = FDCAN_IR_RF0N | FDCAN_IR_TC | FDCAN_IR_BO | FDCAN_IR_EW | FDCAN_IR_EP;
    fdcan->ILS = 0u;            /* 所有中断映射到 Line 0 */
    fdcan->ILE = FDCAN_ILE_EINT0;
    fdcan->IR = fdcan->IR;      /* 清所有挂起中断 */

    /* NVIC */
    NVIC_SetPriority(cfg->irqn, cfg->irq_priority);
    NVIC_EnableIRQ(cfg->irqn);

    return BM_OK;
}

/* -------------------------------------------------------------------------- */
/*  反初始化 / 复位                                                               */
/* -------------------------------------------------------------------------- */

static int bm_can_stm32g4_hw_deinit(bm_can_stm32g4_context_t *ctx) {
    const bm_can_stm32g4_config_t *cfg = ctx->cfg;
    FDCAN_GlobalTypeDef *fdcan;

    if (cfg == NULL) {
        return BM_ERR_INVALID;
    }
    fdcan = cfg->fdcan;

    NVIC_DisableIRQ(cfg->irqn);
    if (bm_can_stm32g4_enter_init(fdcan) != BM_OK) {
        return BM_ERR_TIMEOUT;
    }

    fdcan->CCCR = FDCAN_CCCR_INIT | FDCAN_CCCR_CCE;
    fdcan->IE = 0u;
    fdcan->ILE = 0u;
    fdcan->IR = fdcan->IR;

    /* 时钟由 FDCAN1/FDCAN2 共享，这里不关闭，避免影响另一实例 */
    return BM_OK;
}

/* -------------------------------------------------------------------------- */
/*  过滤器配置                                                                  */
/* -------------------------------------------------------------------------- */

static int bm_can_stm32g4_add_std_filter(bm_can_stm32g4_context_t *ctx,
                                         const bm_can_filter_t *filter,
                                         uint32_t *filter_id) {
    const bm_can_stm32g4_config_t *cfg = ctx->cfg;
    FDCAN_GlobalTypeDef *fdcan = cfg->fdcan;
    volatile uint32_t *sf_addr;
    uint32_t sfec;
    uint32_t sft;
    uint32_t word;
    uint32_t idx;
    uint32_t i;

    if (filter->fifo > BM_CAN_FILTER_FIFO1) {
        return BM_ERR_INVALID;
    }
    if (filter->id > BM_CAN_STD_ID_MAX || filter->mask > BM_CAN_STD_ID_MAX) {
        return BM_ERR_INVALID;
    }

    sf_addr = bm_can_stm32g4_msg_ram(cfg, BM_CAN_G4_FLSSA);
    idx = cfg->std_filter_count;
    for (i = 0u; i < cfg->std_filter_count; ++i) {
        /* 检查是否已禁用（SFEC=0） */
        if ((sf_addr[i] & (0x7u << 27u)) == 0u) {
            idx = i;
            break;
        }
    }
    if (idx >= cfg->std_filter_count) {
        return BM_ERR_NO_MEM;
    }

    sfec = (filter->fifo == BM_CAN_FILTER_FIFO0) ? FDCAN_SFEC_FIFO0 : FDCAN_SFEC_FIFO1;

    switch (filter->type) {
    case BM_CAN_FILTER_TYPE_RANGE:
        sft = FDCAN_SFT_RANGE;
        break;
    case BM_CAN_FILTER_TYPE_MASK:
        sft = FDCAN_SFT_MASK;
        break;
    case BM_CAN_FILTER_TYPE_LIST:
        sft = FDCAN_SFT_DUAL;
        break;
    default:
        return BM_ERR_INVALID;
    }

    word = ((sft & 0x3u) << 30u) |
           ((sfec & 0x7u) << 27u) |
           ((filter->id & 0x7FFu) << 16u) |
           (filter->mask & 0x7FFu);

    sf_addr[idx] = word;

    if (filter_id != NULL) {
        *filter_id = idx;
    }

    /* 若目标为 FIFO1，使能 FIFO1 新消息中断 */
    if (filter->fifo == BM_CAN_FILTER_FIFO1) {
        fdcan->IE |= FDCAN_IR_RF1N;
    }

    return BM_OK;
}

static int bm_can_stm32g4_add_ext_filter(bm_can_stm32g4_context_t *ctx,
                                         const bm_can_filter_t *filter,
                                         uint32_t *filter_id) {
    const bm_can_stm32g4_config_t *cfg = ctx->cfg;
    FDCAN_GlobalTypeDef *fdcan = cfg->fdcan;
    volatile uint32_t *ef_addr;
    uint32_t efec;
    uint32_t eft;
    uint32_t word0, word1;
    uint32_t idx;
    uint32_t i;

    if (cfg->ext_filter_count == 0u) {
        return BM_ERR_NOT_SUPPORTED;
    }
    if (filter->fifo > BM_CAN_FILTER_FIFO1) {
        return BM_ERR_INVALID;
    }
    if (filter->id > BM_CAN_EXT_ID_MAX || filter->mask > BM_CAN_EXT_ID_MAX) {
        return BM_ERR_INVALID;
    }

    ef_addr = bm_can_stm32g4_msg_ram(cfg, BM_CAN_G4_FLESA);
    idx = cfg->ext_filter_count;
    for (i = 0u; i < cfg->ext_filter_count; ++i) {
        if ((ef_addr[i * 2u] & (0x7u << 29u)) == 0u) {
            idx = i;
            break;
        }
    }
    if (idx >= cfg->ext_filter_count) {
        return BM_ERR_NO_MEM;
    }

    efec = (filter->fifo == BM_CAN_FILTER_FIFO0) ? FDCAN_EFEC_FIFO0 : FDCAN_EFEC_FIFO1;

    switch (filter->type) {
    case BM_CAN_FILTER_TYPE_RANGE:
        eft = FDCAN_EFT_RANGE;
        break;
    case BM_CAN_FILTER_TYPE_MASK:
        eft = FDCAN_EFT_MASK;
        break;
    case BM_CAN_FILTER_TYPE_LIST:
        eft = FDCAN_EFT_DUAL;
        break;
    default:
        return BM_ERR_INVALID;
    }

    word0 = ((eft & 0x3u) << 30u) |
            ((efec & 0x7u) << 29u) |
            (filter->id & 0x1FFFFFFFu);
    word1 = filter->mask & 0x1FFFFFFFu;

    ef_addr[idx * 2u] = word0;
    ef_addr[idx * 2u + 1u] = word1;

    if (filter_id != NULL) {
        *filter_id = idx + cfg->std_filter_count;
    }

    if (filter->fifo == BM_CAN_FILTER_FIFO1) {
        fdcan->IE |= FDCAN_IR_RF1N;
    }

    return BM_OK;
}

/* -------------------------------------------------------------------------- */
/*  发送                                                                        */
/* -------------------------------------------------------------------------- */

static int bm_can_stm32g4_validate_frame(const bm_can_frame_t *frame,
                                         int fd_allowed) {
    if (frame->dlc > BM_CAN_MAX_DLC) {
        return BM_ERR_INVALID;
    }
    if ((frame->flags & BM_CAN_FLAG_FD) != 0u && !fd_allowed) {
        return BM_ERR_NOT_SUPPORTED;
    }
    if ((frame->flags & BM_CAN_FLAG_FD) == 0u && frame->dlc > 8u) {
        return BM_ERR_INVALID;
    }
    if ((frame->flags & BM_CAN_FLAG_EXT) != 0u) {
        if (frame->id > BM_CAN_EXT_ID_MAX) {
            return BM_ERR_INVALID;
        }
    } else {
        if (frame->id > BM_CAN_STD_ID_MAX) {
            return BM_ERR_INVALID;
        }
    }
    return BM_OK;
}

static int bm_can_stm32g4_send(const struct bm_hal_can *dev,
                               const bm_can_frame_t *frame) {
    bm_can_stm32g4_context_t *ctx = bm_can_stm32g4_ctx_for(dev);
    const bm_can_stm32g4_config_t *cfg;
    FDCAN_GlobalTypeDef *fdcan;
    volatile uint32_t *tx_addr;
    uint32_t put_idx;
    uint32_t word0, word1;
    uint32_t dlc_code;
    uint32_t data_words;
    uint32_t status;
    uint32_t i;
    int rc;

    if (ctx == NULL || ctx->cfg == NULL) {
        return BM_ERR_INVALID;
    }
    cfg = ctx->cfg;
    fdcan = cfg->fdcan;

    if (!ctx->initialized) {
        return BM_ERR_NOT_INIT;
    }
    if (!ctx->started) {
        return BM_ERR_INVALID;
    }

    rc = bm_can_stm32g4_validate_frame(frame, cfg->fd_enabled != 0u);
    if (rc != BM_OK) {
        return rc;
    }

    status = fdcan->TXFQS;
    if ((status & FDCAN_TXFQS_TFQF_Msk) != 0u) {
        return BM_ERR_BUSY;
    }
    put_idx = (status >> 16u) & 0x3Fu; /* TFQPI field */

    dlc_code = bm_can_bytes_to_dlc(frame->dlc);
    data_words = (bm_can_dlc_to_bytes[dlc_code] + 3u) / 4u;

    /* Word 0: ID + flags */
    if ((frame->flags & BM_CAN_FLAG_EXT) != 0u) {
        word0 = FDCAN_ELEMENT_MASK_XTD | (frame->id & FDCAN_ELEMENT_MASK_EXTID);
    } else {
        word0 = ((frame->id & 0x7FFu) << 18u);
    }
    if ((frame->flags & BM_CAN_FLAG_RTR) != 0u) {
        word0 |= FDCAN_ELEMENT_MASK_RTR;
    }
    if ((frame->flags & BM_CAN_FLAG_FD) != 0u) {
        word0 |= FDCAN_ELEMENT_MASK_FDF;
    }

    /* Word 1: DLC + Message Marker（T1[31:24]；T1[7:0] 为保留位，不可写） */
    word1 = ((uint32_t)dlc_code << 16u) | ((put_idx & 0xFFu) << 24u);
    if ((frame->flags & BM_CAN_FLAG_BRS) != 0u) {
        word1 |= FDCAN_ELEMENT_MASK_BRS;
    }

    tx_addr = bm_can_stm32g4_msg_ram(cfg,
                                     BM_CAN_G4_TFQSA +
                                     (put_idx * BM_CAN_G4_TFQ_SIZE));

    tx_addr[0] = word0;
    tx_addr[1] = word1;
    for (i = 0u; i < data_words; ++i) {
        tx_addr[2 + i] = ((uint32_t)frame->data[i * 4 + 0]) |
                         ((uint32_t)frame->data[i * 4 + 1] << 8u) |
                         ((uint32_t)frame->data[i * 4 + 2] << 16u) |
                         ((uint32_t)frame->data[i * 4 + 3] << 24u);
    }

    /* 请求发送 */
    fdcan->TXBAR = ((uint32_t)1u << put_idx);
    ctx->tx_put_index = put_idx;
    ctx->stats.tx_count++;

    return BM_OK;
}

/* -------------------------------------------------------------------------- */
/*  ISR 处理                                                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief 从 RX FIFO0 读取一帧。
 */
static void bm_can_stm32g4_rx_fifo0_read(bm_can_stm32g4_context_t *ctx) {
    const bm_can_stm32g4_config_t *cfg = ctx->cfg;
    FDCAN_GlobalTypeDef *fdcan = cfg->fdcan;
    volatile uint32_t *rx_addr;
    bm_can_frame_t frame;
    uint32_t get_idx;
    uint32_t word0, word1;
    uint32_t dlc_code;
    uint32_t data_words;
    uint32_t i;

    if ((fdcan->RXF0S & 0x7Fu) == 0u) {
        return;
    }

    get_idx = (fdcan->RXF0S >> 8u) & 0x3Fu; /* F0GI field */

    rx_addr = bm_can_stm32g4_msg_ram(cfg,
                                     BM_CAN_G4_RF0SA +
                                     (get_idx * BM_CAN_G4_RF0_SIZE));

    word0 = rx_addr[0];
    word1 = rx_addr[1];

    (void)memset(&frame, 0, sizeof(frame));

    if ((word0 & FDCAN_ELEMENT_MASK_XTD) != 0u) {
        frame.flags |= BM_CAN_FLAG_EXT;
        frame.id = word0 & FDCAN_ELEMENT_MASK_EXTID;
    } else {
        frame.id = (word0 >> 18u) & 0x7FFu;
    }
    if ((word0 & FDCAN_ELEMENT_MASK_RTR) != 0u) {
        frame.flags |= BM_CAN_FLAG_RTR;
    }
    if ((word1 & FDCAN_ELEMENT_MASK_FDF) != 0u) {
        frame.flags |= BM_CAN_FLAG_FD;
    }
    if ((word1 & FDCAN_ELEMENT_MASK_BRS) != 0u) {
        frame.flags |= BM_CAN_FLAG_BRS;
    }

    dlc_code = (word1 >> 16u) & 0xFu;
    frame.dlc = bm_can_dlc_to_bytes[dlc_code];

    data_words = (frame.dlc + 3u) / 4u;
    for (i = 0u; i < data_words; ++i) {
        uint32_t dw = rx_addr[2 + i];
        frame.data[i * 4 + 0] = (uint8_t)(dw);
        frame.data[i * 4 + 1] = (uint8_t)(dw >> 8u);
        frame.data[i * 4 + 2] = (uint8_t)(dw >> 16u);
        frame.data[i * 4 + 3] = (uint8_t)(dw >> 24u);
    }

    /* 真实时间戳 */
    frame.timestamp_us = bm_uptime_us();

    /* 确认读取 */
    fdcan->RXF0A = get_idx;

    ctx->stats.rx_count++;
    if (ctx->rx_cb != NULL) {
        ctx->rx_cb(ctx->dev, &frame, ctx->rx_user);
    }
}

/**
 * @brief RX FIFO0 ISR：有界循环读取所有待处理帧。
 */
static void bm_can_stm32g4_rx_fifo0_isr(bm_can_stm32g4_context_t *ctx) {
    FDCAN_GlobalTypeDef *fdcan = ctx->cfg->fdcan;
    uint32_t iter = 0u;

    while (((fdcan->RXF0S & 0x7Fu) != 0u) && (iter < 8u)) {
        bm_can_stm32g4_rx_fifo0_read(ctx);
        iter++;
    }
}

/**
 * @brief 从 RX FIFO1 读取一帧（仿 rx_fifo0_read，寄存器换为 RXF1S/RXF1A）。
 */
static void bm_can_stm32g4_rx_fifo1_read(bm_can_stm32g4_context_t *ctx) {
    const bm_can_stm32g4_config_t *cfg = ctx->cfg;
    FDCAN_GlobalTypeDef *fdcan = cfg->fdcan;
    volatile uint32_t *rx_addr;
    bm_can_frame_t frame;
    uint32_t get_idx;
    uint32_t word0, word1;
    uint32_t dlc_code;
    uint32_t data_words;
    uint32_t i;

    if ((fdcan->RXF1S & 0x7Fu) == 0u) {
        return;
    }

    get_idx = (fdcan->RXF1S >> 8u) & 0x3Fu; /* F1GI field */

    rx_addr = bm_can_stm32g4_msg_ram(cfg,
                                     BM_CAN_G4_RF1SA +
                                     (get_idx * BM_CAN_G4_RF1_SIZE));

    word0 = rx_addr[0];
    word1 = rx_addr[1];

    (void)memset(&frame, 0, sizeof(frame));

    if ((word0 & FDCAN_ELEMENT_MASK_XTD) != 0u) {
        frame.flags |= BM_CAN_FLAG_EXT;
        frame.id = word0 & FDCAN_ELEMENT_MASK_EXTID;
    } else {
        frame.id = (word0 >> 18u) & 0x7FFu;
    }
    if ((word0 & FDCAN_ELEMENT_MASK_RTR) != 0u) {
        frame.flags |= BM_CAN_FLAG_RTR;
    }
    if ((word1 & FDCAN_ELEMENT_MASK_FDF) != 0u) {
        frame.flags |= BM_CAN_FLAG_FD;
    }
    if ((word1 & FDCAN_ELEMENT_MASK_BRS) != 0u) {
        frame.flags |= BM_CAN_FLAG_BRS;
    }

    dlc_code = (word1 >> 16u) & 0xFu;
    frame.dlc = bm_can_dlc_to_bytes[dlc_code];

    data_words = (frame.dlc + 3u) / 4u;
    for (i = 0u; i < data_words; ++i) {
        uint32_t dw = rx_addr[2 + i];
        frame.data[i * 4 + 0] = (uint8_t)(dw);
        frame.data[i * 4 + 1] = (uint8_t)(dw >> 8u);
        frame.data[i * 4 + 2] = (uint8_t)(dw >> 16u);
        frame.data[i * 4 + 3] = (uint8_t)(dw >> 24u);
    }

    /* 真实时间戳 */
    frame.timestamp_us = bm_uptime_us();

    /* 确认读取 */
    fdcan->RXF1A = get_idx;

    ctx->stats.rx_count++;
    if (ctx->rx_cb != NULL) {
        ctx->rx_cb(ctx->dev, &frame, ctx->rx_user);
    }
}

/**
 * @brief RX FIFO1 ISR：有界循环读取所有待处理帧。
 */
static void bm_can_stm32g4_rx_fifo1_isr(bm_can_stm32g4_context_t *ctx) {
    FDCAN_GlobalTypeDef *fdcan = ctx->cfg->fdcan;
    uint32_t iter = 0u;

    while (((fdcan->RXF1S & 0x7Fu) != 0u) && (iter < 8u)) {
        bm_can_stm32g4_rx_fifo1_read(ctx);
        iter++;
    }
}

/**
 * @brief 更新错误与协议状态统计。
 */
static void bm_can_stm32g4_update_protocol_status(bm_can_stm32g4_context_t *ctx) {
    FDCAN_GlobalTypeDef *fdcan = ctx->cfg->fdcan;
    uint32_t psr = fdcan->PSR;
    uint32_t events = 0u;

    if ((psr & FDCAN_PSR_BO) != 0u && ctx->bus_off == 0) {
        ctx->bus_off = 1;
        ctx->stats.bus_off_count++;
        events |= BM_CAN_EVT_BUS_OFF;
    }
    if ((psr & FDCAN_PSR_EW) != 0u) {
        ctx->stats.error_warning_count++;
        events |= BM_CAN_EVT_ERROR_WARNING;
    }
    if ((psr & FDCAN_PSR_EP) != 0u) {
        ctx->stats.error_passive_count++;
        events |= BM_CAN_EVT_ERROR_PASSIVE;
    }
    if (ctx->bus_off != 0 && (psr & FDCAN_PSR_BO) == 0u) {
        ctx->bus_off = 0;
        events |= BM_CAN_EVT_BUS_OFF_RECOVER;
    }

    ctx->stats.last_errors |= events;
    if (events != 0u && ctx->event_cb != NULL) {
        ctx->event_cb(ctx->dev, events, ctx->event_user);
    }
}

/**
 * @brief 公共 ISR。
 */
static void bm_can_stm32g4_isr(FDCAN_GlobalTypeDef *fdcan) {
    bm_can_stm32g4_context_t *ctx = bm_can_stm32g4_ctx_for_fdcan(fdcan);
    uint32_t ir;

    if (ctx == NULL) {
        return;
    }

    ir = fdcan->IR;

    if ((ir & FDCAN_IR_RF0N) != 0u) {
        fdcan->IR = FDCAN_IR_RF0N;
        bm_can_stm32g4_rx_fifo0_isr(ctx);
    }
    if ((ir & (FDCAN_IR_RF0F | FDCAN_IR_RF0L)) != 0u) {
        fdcan->IR = (ir & (FDCAN_IR_RF0F | FDCAN_IR_RF0L));
        if ((ir & FDCAN_IR_RF0F) != 0u) {
            ctx->stats.rx_overflow_count++;
            ctx->stats.last_errors |= BM_CAN_EVT_RX_OVERFLOW;
            if (ctx->event_cb != NULL) {
                ctx->event_cb(ctx->dev, BM_CAN_EVT_RX_OVERFLOW, ctx->event_user);
            }
        }
        if ((ir & FDCAN_IR_RF0L) != 0u) {
            ctx->stats.rx_overflow_count++;
            ctx->stats.last_errors |= BM_CAN_EVT_RX_OVERFLOW;
            if (ctx->event_cb != NULL) {
                ctx->event_cb(ctx->dev, BM_CAN_EVT_RX_OVERFLOW, ctx->event_user);
            }
        }
    }
    if ((ir & FDCAN_IR_RF1N) != 0u) {
        fdcan->IR = FDCAN_IR_RF1N;
        bm_can_stm32g4_rx_fifo1_isr(ctx);
    }
    if ((ir & (FDCAN_IR_RF1F | FDCAN_IR_RF1L)) != 0u) {
        fdcan->IR = (ir & (FDCAN_IR_RF1F | FDCAN_IR_RF1L));
        ctx->stats.rx_overflow_count++;
        ctx->stats.last_errors |= BM_CAN_EVT_RX_OVERFLOW;
        if (ctx->event_cb != NULL) {
            ctx->event_cb(ctx->dev, BM_CAN_EVT_RX_OVERFLOW, ctx->event_user);
        }
    }
    if ((ir & FDCAN_IR_TC) != 0u) {
        fdcan->IR = FDCAN_IR_TC;
        if (ctx->event_cb != NULL) {
            ctx->event_cb(ctx->dev, BM_CAN_EVT_TX_COMPLETE, ctx->event_user);
        }
    }
    if ((ir & FDCAN_IR_TEFL) != 0u) {
        fdcan->IR = FDCAN_IR_TEFL;
        ctx->stats.tx_timeout_count++;
        ctx->stats.last_errors |= BM_CAN_EVT_TX_TIMEOUT;
        if (ctx->event_cb != NULL) {
            ctx->event_cb(ctx->dev, BM_CAN_EVT_TX_TIMEOUT, ctx->event_user);
        }
    }
    if ((ir & (FDCAN_IR_BO | FDCAN_IR_EW | FDCAN_IR_EP)) != 0u) {
        fdcan->IR = (ir & (FDCAN_IR_BO | FDCAN_IR_EW | FDCAN_IR_EP));
        bm_can_stm32g4_update_protocol_status(ctx);
    }
}

/* -------------------------------------------------------------------------- */
/*  IRQ handlers                                                                */
/* -------------------------------------------------------------------------- */

void FDCAN1_IT0_IRQHandler(void) {
    bm_can_stm32g4_isr(FDCAN1);
}

void FDCAN2_IT0_IRQHandler(void) {
    bm_can_stm32g4_isr(FDCAN2);
}

/* -------------------------------------------------------------------------- */
/*  HAL API 实现                                                                */
/* -------------------------------------------------------------------------- */

static int bm_can_stm32g4_init(const struct bm_hal_can *dev, void *config) {
    bm_can_stm32g4_context_t *ctx;
    const bm_can_stm32g4_config_t *cfg;
    int rc;

    if (dev == NULL) {
        return BM_ERR_INVALID;
    }
    ctx = bm_can_stm32g4_ctx_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }

    /* App 可通过 config 参数覆盖 dev->config；NULL 则使用设备默认值 */
    cfg = (config != NULL)
              ? (const bm_can_stm32g4_config_t *)config
              : (const bm_can_stm32g4_config_t *)dev->config;
    if (cfg == NULL) {
        return BM_ERR_INVALID;
    }

    /* 先清零再初始化；硬件初始化失败时不置 initialized */
    ctx->dev = dev;
    ctx->cfg = cfg;
    ctx->initialized = 0;
    ctx->started = 0;
    ctx->bus_off = 0;
    ctx->tx_put_index = 0u;
    ctx->rx_cb = NULL;
    ctx->rx_user = NULL;
    ctx->event_cb = NULL;
    ctx->event_user = NULL;
    (void)memset(&ctx->stats, 0, sizeof(ctx->stats));

    rc = bm_can_stm32g4_hw_init(ctx);
    if (rc != BM_OK) {
        /* 回滚已配置的 GPIO/时钟/中断 */
        (void)bm_can_stm32g4_hw_deinit(ctx);
        ctx->cfg = NULL;
        return rc;
    }

    ctx->initialized = 1;
    return BM_OK;
}

static int bm_can_stm32g4_start(const struct bm_hal_can *dev) {
    bm_can_stm32g4_context_t *ctx = bm_can_stm32g4_ctx_for(dev);
    FDCAN_GlobalTypeDef *fdcan;

    if (ctx == NULL || ctx->cfg == NULL) {
        return BM_ERR_INVALID;
    }
    if (!ctx->initialized) {
        return BM_ERR_NOT_INIT;
    }
    fdcan = ctx->cfg->fdcan;
    if (bm_can_stm32g4_leave_init(fdcan) != BM_OK) {
        return BM_ERR_TIMEOUT;
    }
    ctx->started = 1;
    return BM_OK;
}

static int bm_can_stm32g4_stop(const struct bm_hal_can *dev) {
    bm_can_stm32g4_context_t *ctx = bm_can_stm32g4_ctx_for(dev);
    FDCAN_GlobalTypeDef *fdcan;

    if (ctx == NULL || ctx->cfg == NULL) {
        return BM_ERR_INVALID;
    }
    if (!ctx->initialized) {
        return BM_ERR_NOT_INIT;
    }
    fdcan = ctx->cfg->fdcan;
    if (bm_can_stm32g4_enter_init(fdcan) != BM_OK) {
        return BM_ERR_TIMEOUT;
    }
    ctx->started = 0;
    return BM_OK;
}

static int bm_can_stm32g4_add_filter(const struct bm_hal_can *dev,
                                     const bm_can_filter_t *filter,
                                     uint32_t *filter_id) {
    bm_can_stm32g4_context_t *ctx = bm_can_stm32g4_ctx_for(dev);

    if (ctx == NULL || ctx->cfg == NULL) {
        return BM_ERR_INVALID;
    }
    if (!ctx->initialized) {
        return BM_ERR_NOT_INIT;
    }
    if (filter == NULL) {
        return BM_ERR_INVALID;
    }
    if (filter->id_format == BM_CAN_FILTER_STD) {
        return bm_can_stm32g4_add_std_filter(ctx, filter, filter_id);
    }
    if (filter->id_format == BM_CAN_FILTER_EXT) {
        return bm_can_stm32g4_add_ext_filter(ctx, filter, filter_id);
    }
    return BM_ERR_INVALID;
}

static int bm_can_stm32g4_remove_filter(const struct bm_hal_can *dev,
                                        uint32_t filter_id) {
    bm_can_stm32g4_context_t *ctx = bm_can_stm32g4_ctx_for(dev);
    const bm_can_stm32g4_config_t *cfg;
    volatile uint32_t *sf_addr;
    volatile uint32_t *ef_addr;

    if (ctx == NULL || ctx->cfg == NULL) {
        return BM_ERR_INVALID;
    }
    if (!ctx->initialized) {
        return BM_ERR_NOT_INIT;
    }
    cfg = ctx->cfg;

    if (filter_id < cfg->std_filter_count) {
        sf_addr = bm_can_stm32g4_msg_ram(cfg, BM_CAN_G4_FLSSA);
        sf_addr[filter_id] = 0u; /* SFEC=0 禁用 */
        return BM_OK;
    }
    filter_id -= cfg->std_filter_count;
    if (filter_id < cfg->ext_filter_count) {
        ef_addr = bm_can_stm32g4_msg_ram(cfg, BM_CAN_G4_FLESA);
        ef_addr[filter_id * 2u] = 0u; /* EFEC=0 禁用 */
        return BM_OK;
    }
    return BM_ERR_INVALID;
}

static uint32_t bm_can_stm32g4_get_capabilities(const struct bm_hal_can *dev) {
    bm_can_stm32g4_context_t *ctx = bm_can_stm32g4_ctx_for(dev);
    uint32_t caps;

    if (ctx == NULL || ctx->cfg == NULL) {
        return 0u;
    }
    caps = BM_CAN_CAP_STD_FILTER | BM_CAN_CAP_FIFO0 | BM_CAN_CAP_TX_FIFO;
    if (ctx->cfg->ext_filter_count > 0u) {
        caps |= BM_CAN_CAP_EXT_FILTER;
    }
    if (ctx->cfg->rx_fifo1_count > 0u) {
        caps |= BM_CAN_CAP_FIFO1;
    }
    if (ctx->cfg->fd_enabled != 0u) {
        caps |= BM_CAN_CAP_FD;
    }
    return caps;
}

static int bm_can_stm32g4_get_stats(const struct bm_hal_can *dev,
                                    bm_can_stats_t *stats) {
    bm_can_stm32g4_context_t *ctx = bm_can_stm32g4_ctx_for(dev);

    if (ctx == NULL || ctx->cfg == NULL || stats == NULL) {
        return BM_ERR_INVALID;
    }
    *stats = ctx->stats;
    ctx->stats.last_errors = 0u;
    return BM_OK;
}

static int bm_can_stm32g4_set_rx_callback(const struct bm_hal_can *dev,
                                          bm_can_rx_callback_t cb, void *user) {
    bm_can_stm32g4_context_t *ctx = bm_can_stm32g4_ctx_for(dev);

    if (ctx == NULL || ctx->cfg == NULL) {
        return BM_ERR_INVALID;
    }
    ctx->rx_cb = cb;
    ctx->rx_user = user;
    return BM_OK;
}

static int bm_can_stm32g4_set_event_callback(const struct bm_hal_can *dev,
                                             bm_can_event_callback_t cb,
                                             void *user) {
    bm_can_stm32g4_context_t *ctx = bm_can_stm32g4_ctx_for(dev);

    if (ctx == NULL || ctx->cfg == NULL) {
        return BM_ERR_INVALID;
    }
    ctx->event_cb = cb;
    ctx->event_user = user;
    return BM_OK;
}

/* -------------------------------------------------------------------------- */
/*  Vendor 特定 API                                                             */
/* -------------------------------------------------------------------------- */

int bm_can_stm32g4_recover(const struct bm_hal_can *dev) {
    bm_can_stm32g4_context_t *ctx = bm_can_stm32g4_ctx_for(dev);
    FDCAN_GlobalTypeDef *fdcan;

    if (ctx == NULL || ctx->cfg == NULL) {
        return BM_ERR_INVALID;
    }
    if (!ctx->initialized) {
        return BM_ERR_NOT_INIT;
    }
    fdcan = ctx->cfg->fdcan;

    if (bm_can_stm32g4_enter_init(fdcan) != BM_OK) {
        return BM_ERR_TIMEOUT;
    }
    fdcan->IR = FDCAN_IR_BO;
    if (bm_can_stm32g4_leave_init(fdcan) != BM_OK) {
        return BM_ERR_TIMEOUT;
    }
    /* 不在此强制清 ctx->bus_off：PSR.BO 未真正清零时强制清会导致
     * update_protocol_status 重复上报 bus-off；由其在 PSR.BO 消失时统一清除 */
    return BM_OK;
}

int bm_can_stm32g4_get_bitrate(const struct bm_hal_can *dev,
                               uint32_t *bitrate_bps,
                               uint32_t *sample_pt_promille) {
    bm_can_stm32g4_context_t *ctx = bm_can_stm32g4_ctx_for(dev);
    uint32_t bitrate;
    uint32_t sample_pt;

    if (ctx == NULL || ctx->cfg == NULL) {
        return BM_ERR_INVALID;
    }

    bm_can_stm32g4_calc_bitrate(ctx->cfg, &bitrate, &sample_pt);

    if (bitrate_bps != NULL) {
        *bitrate_bps = bitrate;
    }
    if (sample_pt_promille != NULL) {
        *sample_pt_promille = sample_pt;
    }
    return BM_OK;
}

/* -------------------------------------------------------------------------- */
/*  驱动 API 表与默认实例                                                       */
/* -------------------------------------------------------------------------- */

static const struct bm_can_driver_api g_stm32g4_can_api = {
    bm_can_stm32g4_init,
    bm_can_stm32g4_start,
    bm_can_stm32g4_stop,
    bm_can_stm32g4_send,
    bm_can_stm32g4_add_filter,
    bm_can_stm32g4_remove_filter,
    bm_can_stm32g4_get_capabilities,
    bm_can_stm32g4_get_stats,
    bm_can_stm32g4_set_rx_callback,
    bm_can_stm32g4_set_event_callback,
};

const struct bm_hal_can bm_stm32g4_can1 = { &g_stm32g4_can_api, &g_can_cfg_1 };
const struct bm_hal_can bm_stm32g4_can2 = { &g_stm32g4_can_api, &g_can_cfg_2 };
