/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_drv_nvs_flash_stm32g4.c
 * @brief STM32G4 主 Flash 双槽 NVS 后端（bm_hal_nvs_load/save）
 *
 * 原子语义：save 写入 inactive 槽（先擦除覆盖页、再双字编程、再读回校验）；
 * 掉电则保留旧 active。分区地址由 Board 经 `bm_nvs_stm32g4_set_layout` 注入。
 *
 * Flash 操作按 RM0440：解锁 → 页擦除（含双 Bank BKER）→ 64-bit 编程 → 上锁。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-28       1.0            zeh            新增 G4 Flash 双槽 NVS 后端
 *
 */
#include "bm_hal_nvs_stm32g4.h"
#include "bm_nvs_dual_slot.h"
#include "hal/bm_hal_nvs.h"
#include "bm/common/bm_types.h"

#include <string.h>

#include "stm32g4xx.h"

#ifndef FLASH_PAGE_SIZE
#define FLASH_PAGE_SIZE 0x800u
#endif

#ifndef FLASH_BASE
#define FLASH_BASE 0x08000000u
#endif

/** @brief 擦除/编程轮询上限（有界路径）。 */
#define BM_NVS_FLASH_BUSY_LIMIT  2000000u

/** @brief 布局状态。 */
static uint32_t s_base_a;
static uint32_t s_base_b;
static uint32_t s_slot_size;
static int      s_layout_ok;

/** @brief 工作缓冲：单槽打包（BSS；slot_size 上限由布局约束，此处用最大页×4 保守）。 */
#ifndef BM_NVS_STM32G4_SLOT_BUF_MAX
#define BM_NVS_STM32G4_SLOT_BUF_MAX  4096u
#endif
static uint8_t s_slot_buf[BM_NVS_STM32G4_SLOT_BUF_MAX];
static uint8_t s_slot_a_cache[BM_NVS_STM32G4_SLOT_BUF_MAX];
static uint8_t s_slot_b_cache[BM_NVS_STM32G4_SLOT_BUF_MAX];

/**
 * @brief 地址是否落在主 Flash 且 8 字节对齐。
 */
static int bm_nvs_flash_addr_ok(uint32_t addr, uint32_t len) {
    uint32_t flash_end;

    if ((addr & 7u) != 0u) {
        return 0;
    }
    if (len == 0u || addr < FLASH_BASE) {
        return 0;
    }
#if defined(FLASH_SIZE)
    flash_end = FLASH_BASE + (uint32_t)FLASH_SIZE;
#else
    /* 无宏时按 G474 512KB 上界保守校验；Board 仍须保证真实容量内 */
    flash_end = FLASH_BASE + (512u * 1024u);
#endif
    if (addr > flash_end || len > (flash_end - addr)) {
        return 0;
    }
    return 1;
}

int bm_nvs_stm32g4_set_layout(uint32_t base_a, uint32_t base_b,
                              uint32_t slot_size) {
    if (slot_size == 0u || slot_size > BM_NVS_STM32G4_SLOT_BUF_MAX) {
        return BM_ERR_INVALID;
    }
    if (bm_nvs_flash_addr_ok(base_a, slot_size) == 0 ||
        bm_nvs_flash_addr_ok(base_b, slot_size) == 0) {
        return BM_ERR_INVALID;
    }
    /* 区间不重叠 */
    if (!(base_a + slot_size <= base_b || base_b + slot_size <= base_a)) {
        return BM_ERR_INVALID;
    }

    s_base_a = base_a;
    s_base_b = base_b;
    s_slot_size = slot_size;
    s_layout_ok = 1;
    return BM_OK;
}

/**
 * @brief 等待 FLASH BSY 清零；超时或错误返回非零。
 */
static int bm_nvs_flash_wait_ready(void) {
    uint32_t n = BM_NVS_FLASH_BUSY_LIMIT;

    while ((FLASH->SR & FLASH_SR_BSY) != 0u) {
        if (n == 0u) {
            return BM_ERR_TIMEOUT;
        }
        --n;
    }
    if ((FLASH->SR & (FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR |
                      FLASH_SR_SIZERR | FLASH_SR_PGSERR | FLASH_SR_MISERR |
                      FLASH_SR_FASTERR)) != 0u) {
        FLASH->SR = FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR |
                    FLASH_SR_SIZERR | FLASH_SR_PGSERR | FLASH_SR_MISERR |
                    FLASH_SR_FASTERR;
        return BM_ERR_IO;
    }
    return BM_OK;
}

/**
 * @brief 解锁 Flash 控制器。
 */
static int bm_nvs_flash_unlock(void) {
    if ((FLASH->CR & FLASH_CR_LOCK) == 0u) {
        return BM_OK;
    }
    FLASH->KEYR = 0x45670123u;
    FLASH->KEYR = 0xCDEF89ABu;
    if ((FLASH->CR & FLASH_CR_LOCK) != 0u) {
        return BM_ERR_IO;
    }
    return BM_OK;
}

/**
 * @brief 上锁 Flash。
 */
static void bm_nvs_flash_lock(void) {
    FLASH->CR |= FLASH_CR_LOCK;
}

/**
 * @brief 擦除覆盖 [addr, addr+len) 的全部页。
 */
static int bm_nvs_flash_erase_range(uint32_t addr, uint32_t len) {
    uint32_t end = addr + len;
    uint32_t page_addr;
    int rc;

    rc = bm_nvs_flash_wait_ready();
    if (rc != BM_OK) {
        return rc;
    }

    for (page_addr = addr - (addr % FLASH_PAGE_SIZE);
         page_addr < end;
         page_addr += FLASH_PAGE_SIZE) {
        uint32_t page;
        uint32_t bank_pages;

#if defined(FLASH_OPTR_DBANK)
        if ((FLASH->OPTR & FLASH_OPTR_DBANK) != 0u) {
            bank_pages = ((uint32_t)FLASH_SIZE / 2u) / FLASH_PAGE_SIZE;
            if ((page_addr - FLASH_BASE) >= ((uint32_t)FLASH_SIZE / 2u)) {
                page = ((page_addr - FLASH_BASE) -
                        ((uint32_t)FLASH_SIZE / 2u)) / FLASH_PAGE_SIZE;
                FLASH->CR |= FLASH_CR_BKER;
            } else {
                page = (page_addr - FLASH_BASE) / FLASH_PAGE_SIZE;
                FLASH->CR &= ~FLASH_CR_BKER;
            }
            (void)bank_pages;
        } else {
            page = (page_addr - FLASH_BASE) / FLASH_PAGE_SIZE;
            FLASH->CR &= ~FLASH_CR_BKER;
        }
#else
        page = (page_addr - FLASH_BASE) / FLASH_PAGE_SIZE;
#endif

        FLASH->SR = FLASH_SR_PROGERR | FLASH_SR_WRPERR | FLASH_SR_PGAERR |
                    FLASH_SR_SIZERR | FLASH_SR_PGSERR | FLASH_SR_MISERR |
                    FLASH_SR_FASTERR;
        FLASH->CR &= ~FLASH_CR_PNB;
        FLASH->CR |= ((page & 0xFFu) << FLASH_CR_PNB_Pos);
        FLASH->CR |= FLASH_CR_PER;
        FLASH->CR |= FLASH_CR_STRT;

        rc = bm_nvs_flash_wait_ready();
        FLASH->CR &= ~(FLASH_CR_PER | FLASH_CR_PNB);
        if (rc != BM_OK) {
            return rc;
        }
    }
    return BM_OK;
}

/**
 * @brief 向 Flash 编程 len 字节（不足 8 字节尾部填 0xFF）。
 */
static int bm_nvs_flash_program(uint32_t addr, const uint8_t *data,
                                uint32_t len) {
    uint32_t off = 0u;
    int rc;

    rc = bm_nvs_flash_wait_ready();
    if (rc != BM_OK) {
        return rc;
    }

    while (off < len) {
        uint8_t chunk[8];
        uint32_t n = len - off;
        uint32_t i;

        for (i = 0u; i < 8u; ++i) {
            chunk[i] = 0xFFu;
        }
        for (i = 0u; i < n && i < 8u; ++i) {
            chunk[i] = data[off + i];
        }

        FLASH->CR |= FLASH_CR_PG;
        *(volatile uint32_t *)(addr + off) =
            (uint32_t)chunk[0] | ((uint32_t)chunk[1] << 8) |
            ((uint32_t)chunk[2] << 16) | ((uint32_t)chunk[3] << 24);
        __DSB();
        *(volatile uint32_t *)(addr + off + 4u) =
            (uint32_t)chunk[4] | ((uint32_t)chunk[5] << 8) |
            ((uint32_t)chunk[6] << 16) | ((uint32_t)chunk[7] << 24);
        __DSB();

        rc = bm_nvs_flash_wait_ready();
        FLASH->CR &= ~FLASH_CR_PG;
        if (rc != BM_OK) {
            return rc;
        }
        off += 8u;
    }
    return BM_OK;
}

/**
 * @brief 从 Flash 拷贝槽到 RAM。
 */
static void bm_nvs_flash_read_slot(uint32_t base, uint8_t *dst) {
    (void)memcpy(dst, (const void *)base, s_slot_size);
}

int bm_hal_nvs_load(uint8_t *buf, uint16_t size) {
    int which = 0;
    uint32_t seq = 0u;
    const uint8_t *payload = NULL;
    int rc;

    if (buf == NULL || size == 0u) {
        return BM_ERR_INVALID;
    }
    if (s_layout_ok == 0) {
        return BM_ERR_NOT_INIT;
    }
    if (bm_nvs_slot_min_size(size) > s_slot_size) {
        return BM_ERR_OVERFLOW;
    }

    bm_nvs_flash_read_slot(s_base_a, s_slot_a_cache);
    bm_nvs_flash_read_slot(s_base_b, s_slot_b_cache);

    rc = bm_nvs_dual_pick_active(s_slot_a_cache, s_slot_b_cache, s_slot_size,
                                 size, &which, &seq);
    (void)seq;
    if (rc != BM_OK) {
        return rc;
    }

    rc = bm_nvs_slot_parse(which == 0 ? s_slot_a_cache : s_slot_b_cache,
                           s_slot_size, size, NULL, &payload);
    if (rc != BM_OK || payload == NULL) {
        return BM_ERR_INVALID;
    }
    (void)memcpy(buf, payload, size);
    return BM_OK;
}

int bm_hal_nvs_save(const uint8_t *buf, uint16_t size) {
    int which = 0;
    uint32_t seq = 0u;
    uint32_t next_seq;
    uint32_t target;
    int inactive;
    int rc;

    if (buf == NULL || size == 0u) {
        return BM_ERR_INVALID;
    }
    if (s_layout_ok == 0) {
        return BM_ERR_NOT_INIT;
    }
    if (bm_nvs_slot_min_size(size) > s_slot_size) {
        return BM_ERR_OVERFLOW;
    }

    bm_nvs_flash_read_slot(s_base_a, s_slot_a_cache);
    bm_nvs_flash_read_slot(s_base_b, s_slot_b_cache);

    rc = bm_nvs_dual_pick_active(s_slot_a_cache, s_slot_b_cache, s_slot_size,
                                 size, &which, &seq);
    if (rc == BM_OK) {
        next_seq = seq + 1u;
        if (next_seq == 0u) {
            next_seq = 1u; /* 翻转避开 0 作为“无序号”语义 */
        }
        inactive = (which == 0) ? 1 : 0;
    } else if (rc == BM_ERR_NOT_FOUND) {
        next_seq = 1u;
        inactive = 0; /* 首次写 A */
    } else {
        return rc;
    }

    target = (inactive == 0) ? s_base_a : s_base_b;

    (void)memset(s_slot_buf, 0xFF, s_slot_size);
    rc = bm_nvs_slot_pack(s_slot_buf, s_slot_size, next_seq, buf, size);
    if (rc != BM_OK) {
        return rc;
    }

    rc = bm_nvs_flash_unlock();
    if (rc != BM_OK) {
        return rc;
    }

    rc = bm_nvs_flash_erase_range(target, s_slot_size);
    if (rc != BM_OK) {
        bm_nvs_flash_lock();
        return rc;
    }

    rc = bm_nvs_flash_program(target, s_slot_buf,
                              bm_nvs_slot_min_size(size));
    bm_nvs_flash_lock();
    if (rc != BM_OK) {
        return rc;
    }

    /* 读回校验 */
    bm_nvs_flash_read_slot(target, s_slot_a_cache);
    if (bm_nvs_slot_parse(s_slot_a_cache, s_slot_size, size, &seq, NULL)
        != BM_OK || seq != next_seq) {
        return BM_ERR_OVERFLOW;
    }
    if (memcmp(&s_slot_a_cache[BM_NVS_SLOT_HDR_SIZE], buf, size) != 0) {
        return BM_ERR_OVERFLOW;
    }
    return BM_OK;
}
