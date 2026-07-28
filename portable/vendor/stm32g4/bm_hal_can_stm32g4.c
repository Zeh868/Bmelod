/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_hal_can_stm32g4.c
 * @brief STM32G4 FDCAN1/FDCAN2 后端（寄存器级直接操作，等效 LL 风格）
 *
 * STM32CubeG4 未提供 FDCAN LL 头文件，因此本后端基于 CMSIS 寄存器定义直接操作，
 * 仅依赖 `stm32g4xx.h`、`stm32g4xx_ll_bus.h`/`ll_gpio.h`/`ll_rcc.h` 做时钟/GPIO。
 *
 * STM32G4 的 FDCAN Message RAM 布局为**硬件固定**（每个实例 212 words，约 848 bytes）：
 * - 标准过滤器 28 个（1 word/个）
 * - 扩展过滤器 8 个（2 words/个）
 * - RX FIFO0 / RX FIFO1 各 3 个元素（18 words/个，64 byte payload）
 * - TX Event FIFO 3 个元素（2 words/个）
 * - TX FIFO/Queue 3 个元素（18 words/个，64 byte payload）
 *
 * 因此 `bm_can_stm32g4_config_t` 中的 *_count 字段只决定实际使能数量，不能超过
 * 上述硬件上限；本后端在 init 时做校验。
 *
 * 当前能力：
 * - FDCAN1/FDCAN2 多实例，默认 PB8/PB9 与 PB12/PB13（AF9）。
 * - Classic CAN 发送/接收；CAN FD 能力标志仅在 `fd_enabled != 0` 时使能硬件。
 * - 硬件过滤器：标准帧范围/掩码/列表；扩展帧暂不支持（返回 NOT_SUPPORTED）。
 * - RX FIFO0、TX FIFO，ISR 仅做帧搬运与事件派发。
 * - Bus-off / error warning / error passive 检测。
 *
 * 应用通过 `bm_can_stm32g4_config_t` 指定 FDCANx/引脚/位时序；Bmelod 不固定
 * FDCAN 编号与产品引脚。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 STM32G4 FDCAN 寄存器级后端
 */
#include "bm_vendor_can_stm32g4.h"
#include "bm_hal_instances_stm32g4.h"
#include "bm/common/bm_types.h"
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

/* ---------- STM32G4 固定 Message RAM 布局（RM0440 44.3.3 / HAL 内部一致） ---------- */

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

/* TXBC */
#ifndef FDCAN_TXBC_TFQS_Pos
#define FDCAN_TXBC_TFQS_Pos 30u
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
    .message_ram_offset = 0u,
    .std_filter_count = 8u,
    .ext_filter_count = 0u,
    .rx_fifo0_count = BM_CAN_G4_RF0_NBR,
    .rx_fifo1_count = 0u,
    .tx_fifo_count = BM_CAN_G4_TFQ_NBR,
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
    .message_ram_offset = 0u,
    .std_filter_count = 8u,
    .ext_filter_count = 0u,
    .rx_fifo0_count = BM_CAN_G4_RF0_NBR,
    .rx_fifo1_count = 0u,
    .tx_fifo_count = BM_CAN_G4_TFQ_NBR,
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
    const bm_can_stm32g4_config_t *cfg;
    bm_can_stm32g4_context_t *free_slot = NULL;
    uint32_t i;

    if (dev == NULL || dev->config == NULL) {
        return NULL;
    }
    cfg = (const bm_can_stm32g4_config_t *)dev->config;
    for (i = 0u; i < BM_CAN_STM32G4_INSTANCE_COUNT; ++i) {
        if (g_can_ctx[i].cfg == cfg) {
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
 * @brief 由 FDCAN 实例获取 Message RAM 实例索引。
 */
static uint32_t bm_can_stm32g4_instance_index(FDCAN_GlobalTypeDef *fdcan) {
    if (fdcan == FDCAN2) {
        return 1u;
    }
    return 0u;
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
    default: return NULL;
    }
}

/**
 * @brief 配置 CAN TX/RX GPIO 复用。
 */
static void bm_can_stm32g4_gpio_init(const bm_can_stm32g4_config_t *cfg) {
    GPIO_TypeDef *tx_port, *rx_port;
    uint32_t tx_pin, rx_pin;
    uint32_t tx_mask, rx_mask;

    tx_port = bm_can_stm32g4_port(cfg->tx_pin, &tx_pin);
    rx_port = bm_can_stm32g4_port(cfg->rx_pin, &rx_pin);
    if (tx_port == NULL || rx_port == NULL) {
        return;
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
 * @param instance  FDCAN 实例索引（0=FDCAN1，1=FDCAN2）
 * @param offset    实例内 32-bit word 偏移
 */
static volatile uint32_t *bm_can_stm32g4_msg_ram(uint32_t instance,
                                                 uint32_t offset) {
    uint32_t addr = BM_CAN_STM32G4_MSG_RAM_BASE +
                    ((instance * BM_CAN_G4_SIZE) + offset) * 4u;

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
/*  硬件初始化                                                                  */
/* -------------------------------------------------------------------------- */

static int bm_can_stm32g4_hw_init(bm_can_stm32g4_context_t *ctx) {
    const bm_can_stm32g4_config_t *cfg = ctx->cfg;
    FDCAN_GlobalTypeDef *fdcan = cfg->fdcan;
    uint32_t instance = bm_can_stm32g4_instance_index(fdcan);
    uint32_t nbtp, dbtp;
    uint32_t i;

    if (cfg->fdcan != FDCAN1 && cfg->fdcan != FDCAN2) {
        return BM_ERR_INVALID;
    }
    if (cfg->std_filter_count > BM_CAN_G4_FLS_NBR ||
        cfg->ext_filter_count > BM_CAN_G4_FLE_NBR ||
        cfg->rx_fifo0_count > BM_CAN_G4_RF0_NBR ||
        cfg->rx_fifo1_count > BM_CAN_G4_RF1_NBR ||
        cfg->tx_fifo_count > BM_CAN_G4_TFQ_NBR) {
        return BM_ERR_INVALID;
    }

    /* 使能时钟（FDCAN1/FDCAN2 共享 APB1 的 FDCANEN 位） */
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_FDCAN);

    /* 配置 GPIO */
    bm_can_stm32g4_gpio_init(cfg);

    /* 进入 INIT + CCE */
    if (bm_can_stm32g4_enter_init(fdcan) != BM_OK) {
        return BM_ERR_TIMEOUT;
    }

    /* 全局配置 */
    fdcan->CCCR = FDCAN_CCCR_INIT | FDCAN_CCCR_CCE | FDCAN_CCCR_DAR;
    if (cfg->fd_enabled != 0u) {
        fdcan->CCCR |= FDCAN_CCCR_FDOE;
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
        bm_can_stm32g4_msg_ram(instance, i)[0] = 0u;
    }

    /* 全局过滤器：默认拒绝所有非匹配帧（fail-closed），应用通过 add_filter 开通 */
    fdcan->RXGFC = FDCAN_RXGFC_RRFE | FDCAN_RXGFC_RRFS |
                   (2u << FDCAN_RXGFC_ANFE_Pos) |
                   (2u << FDCAN_RXGFC_ANFS_Pos) |
                   ((cfg->std_filter_count & 0xFFu) << FDCAN_RXGFC_LSS_Pos) |
                   ((cfg->ext_filter_count & 0xFFu) << FDCAN_RXGFC_LSE_Pos);

    /* TX FIFO 大小（TXBC 寄存器存在；起始地址固定，只配数量） */
    fdcan->TXBC = ((cfg->tx_fifo_count & 0x3Fu) << FDCAN_TXBC_TFQS_Pos);

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
/*  过滤器配置                                                                  */
/* -------------------------------------------------------------------------- */

static int bm_can_stm32g4_add_std_filter(bm_can_stm32g4_context_t *ctx,
                                         const bm_can_filter_t *filter,
                                         uint32_t *filter_id) {
    const bm_can_stm32g4_config_t *cfg = ctx->cfg;
    FDCAN_GlobalTypeDef *fdcan = cfg->fdcan;
    uint32_t instance = bm_can_stm32g4_instance_index(fdcan);
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

    sf_addr = bm_can_stm32g4_msg_ram(instance, BM_CAN_G4_FLSSA);
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
    uint32_t instance;
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
    instance = bm_can_stm32g4_instance_index(fdcan);

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

    /* Word 1: DLC + marker */
    word1 = ((uint32_t)dlc_code << 16u) | (put_idx & 0xFFu);
    if ((frame->flags & BM_CAN_FLAG_BRS) != 0u) {
        word1 |= FDCAN_ELEMENT_MASK_BRS;
    }

    /* 写入 TX FIFO（固定布局中 TX FIFO/Queue 起始偏移 = BM_CAN_G4_TFQSA） */
    tx_addr = bm_can_stm32g4_msg_ram(instance,
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
 * @brief 读取 RX FIFO0 中的一帧并派发回调。
 */
static void bm_can_stm32g4_rx_fifo0_isr(bm_can_stm32g4_context_t *ctx) {
    const bm_can_stm32g4_config_t *cfg = ctx->cfg;
    FDCAN_GlobalTypeDef *fdcan = cfg->fdcan;
    uint32_t instance = bm_can_stm32g4_instance_index(fdcan);
    volatile uint32_t *rx_addr;
    bm_can_frame_t frame;
    uint32_t get_idx;
    uint32_t word0, word1;
    uint32_t dlc_code;
    uint32_t data_words;
    uint32_t i;

    if ((fdcan->RXF0S & 0xFu) == 0u) {
        return;
    }

    get_idx = (fdcan->RXF0S >> 8u) & 0x3Fu; /* F0GI field */

    rx_addr = bm_can_stm32g4_msg_ram(instance,
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

    /* 时间戳：当前无 TSC 配置，填 0 */
    frame.timestamp_us = 0u;

    /* 确认读取 */
    fdcan->RXF0A = get_idx;

    ctx->stats.rx_count++;
    if (ctx->rx_cb != NULL) {
        ctx->rx_cb(ctx->dev, &frame, ctx->rx_user);
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
        if ((ir & FDCAN_IR_RF0L) != 0u) {
            ctx->stats.rx_overflow_count++;
            ctx->stats.last_errors |= BM_CAN_EVT_RX_OVERFLOW;
            if (ctx->event_cb != NULL) {
                ctx->event_cb(ctx->dev, BM_CAN_EVT_RX_OVERFLOW, ctx->event_user);
            }
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

    (void)config;
    ctx = bm_can_stm32g4_ctx_for(dev);
    if (ctx == NULL) {
        return BM_ERR_INVALID;
    }
    cfg = (const bm_can_stm32g4_config_t *)dev->config;
    ctx->dev = dev;
    ctx->cfg = cfg;
    ctx->initialized = 0;
    ctx->started = 0;
    ctx->bus_off = 0;
    ctx->tx_put_index = 0u;
    (void)memset(&ctx->stats, 0, sizeof(ctx->stats));

    bm_can_stm32g4_hw_init(ctx);

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
    /* 扩展帧过滤器可后续按同样模式扩展 */
    return BM_ERR_NOT_SUPPORTED;
}

static int bm_can_stm32g4_remove_filter(const struct bm_hal_can *dev,
                                        uint32_t filter_id) {
    bm_can_stm32g4_context_t *ctx = bm_can_stm32g4_ctx_for(dev);
    const bm_can_stm32g4_config_t *cfg;
    uint32_t instance;
    volatile uint32_t *sf_addr;

    if (ctx == NULL || ctx->cfg == NULL) {
        return BM_ERR_INVALID;
    }
    cfg = ctx->cfg;
    instance = bm_can_stm32g4_instance_index(cfg->fdcan);
    if (filter_id >= cfg->std_filter_count) {
        return BM_ERR_INVALID;
    }
    sf_addr = bm_can_stm32g4_msg_ram(instance, BM_CAN_G4_FLSSA);
    sf_addr[filter_id] = 0u; /* SFEC=0 禁用 */
    return BM_OK;
}

static uint32_t bm_can_stm32g4_get_capabilities(const struct bm_hal_can *dev) {
    bm_can_stm32g4_context_t *ctx = bm_can_stm32g4_ctx_for(dev);
    uint32_t caps;

    if (ctx == NULL || ctx->cfg == NULL) {
        return 0u;
    }
    caps = BM_CAN_CAP_STD_FILTER | BM_CAN_CAP_FIFO0 | BM_CAN_CAP_TX_FIFO;
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
