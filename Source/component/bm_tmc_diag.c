/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_tmc_diag.c
 * @brief TMC DIAG 通用输入组件实现
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 TMC DIAG 通用输入组件
 * 2026-07-28       1.1            zeh            审查整改：ISR 状态更新入临界区、GPIO 读失败保持上次值、include 全路径
 * 2026-08-01       1.1            Codex           补全 Doxygen 合规注释
 */
#include "bm/component/bm_tmc_diag.h"
#include "bm/common/bm_uptime.h"
#include "bm/common/bm_critical_wrap.h"

#include <stddef.h>

/**
 * @brief 处理 TMC 诊断引脚的外部中断并更新锁存状态
 * @param pin 触发中断的 GPIO 引脚
 * @param user bm_tmc_diag_t 实例指针
 */
static void bm_tmc_diag_exti_cb(uint32_t pin, void *user) {
    bm_tmc_diag_t *diag = (bm_tmc_diag_t *)user;
    bm_irq_state_t irq_state;
    uint64_t event_us;
    int level;
    int active;

    (void)pin;
    if (diag == NULL) {
        return;
    }
    /* GPIO 读失败：保持上次状态，不上报事件（避免 active_low 误锁存） */
    if (bm_hal_gpio_read(diag->config.gpio, diag->config.pin, &level) != BM_OK) {
        return;
    }
    active = diag->config.active_low ? (level == 0) : (level != 0);
    event_us = bm_uptime_us();

    irq_state = BM_CRITICAL_ENTER();
    diag->state.active = active ? 1 : 0;
    if (active) {
        diag->state.latched = 1;
    }
    diag->state.last_event_us = event_us;
    diag->state.event_count++;
    BM_CRITICAL_EXIT(irq_state);

    if (diag->resources.diag_cb != NULL) {
        diag->resources.diag_cb(diag->resources.user, diag->config.pin,
                                event_us);
    }
}

int bm_tmc_diag_validate_config(const bm_tmc_diag_config_t *config) {
    if (config == NULL || config->gpio == NULL) {
        return BM_ERR_INVALID;
    }
    return BM_OK;
}

int bm_tmc_diag_init(bm_tmc_diag_t *diag) {
    int rc;
    uint32_t flags;

    if (diag == NULL) {
        return BM_ERR_INVALID;
    }
    if (bm_tmc_diag_validate_config(&diag->config) != BM_OK) {
        return BM_ERR_INVALID;
    }

    bm_tmc_diag_reset(diag);

    rc = bm_hal_gpio_configure(diag->config.gpio, diag->config.pin,
                               BM_GPIO_INPUT | BM_GPIO_PULL_UP);
    if (rc != BM_OK) {
        return rc;
    }

    flags = diag->config.active_low ? BM_GPIO_EXTI_FALLING : BM_GPIO_EXTI_RISING;
    rc = bm_hal_gpio_exti_configure(diag->config.gpio, diag->config.pin,
                                    flags, bm_tmc_diag_exti_cb, diag);
    if (rc != BM_OK) {
        return rc;
    }
    return bm_hal_gpio_exti_enable(diag->config.gpio, diag->config.pin, 1);
}

void bm_tmc_diag_reset(bm_tmc_diag_t *diag) {
    bm_irq_state_t irq_state;

    if (diag == NULL) {
        return;
    }
    irq_state = BM_CRITICAL_ENTER();
    diag->state.active = 0;
    diag->state.latched = 0;
    diag->state.last_event_us = 0u;
    diag->state.event_count = 0u;
    BM_CRITICAL_EXIT(irq_state);
}

void bm_tmc_diag_clear_latch(bm_tmc_diag_t *diag) {
    bm_irq_state_t irq_state;

    if (diag == NULL) {
        return;
    }
    irq_state = BM_CRITICAL_ENTER();
    diag->state.latched = 0;
    BM_CRITICAL_EXIT(irq_state);
}

int bm_tmc_diag_active(const bm_tmc_diag_t *diag) {
    if (diag == NULL) {
        return 0;
    }
    return diag->state.active;
}

int bm_tmc_diag_latched(const bm_tmc_diag_t *diag) {
    if (diag == NULL) {
        return 0;
    }
    return diag->state.latched;
}
