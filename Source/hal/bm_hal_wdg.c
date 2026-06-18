/**
 * @file bm_hal_wdg.c
 * @brief 看门狗 HAL 分发层（契约 → driver API）
 *
 * 有 BM_DRV_HAS_BACKEND 时转发至 Port driver API；否则提供桩实现。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 *
 */
#include "bm_drv_wdg.h"
#include "bm_hal_wdg.h"
#include "bm_types.h"

#ifdef BM_DRV_HAS_BACKEND
extern const struct bm_wdg_driver_api bm_drv_wdg_api;
#define BM_WDG_DRV (&bm_drv_wdg_api)
#else
static int wdg_stub_init(uint32_t timeout_ms) {
    (void)timeout_ms;
    return BM_ERR_NOT_INIT;
}

static void wdg_stub_feed(void) {
}

static const struct bm_wdg_driver_api wdg_stub = {
    wdg_stub_init,
    wdg_stub_feed,
};

#define BM_WDG_DRV (&wdg_stub)
#endif

int bm_hal_wdg_init(uint32_t timeout_ms) {
    if (!BM_WDG_DRV->init) {
        return BM_ERR_NOT_INIT;
    }
    return BM_WDG_DRV->init(timeout_ms);
}

void bm_hal_wdg_feed(void) {
    if (BM_WDG_DRV->feed) {
        BM_WDG_DRV->feed();
    }
}
