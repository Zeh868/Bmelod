/**
 * @file bm_hal_cpu_stub.c
 * @brief CPU 抽象默认单核桩
 *
 * 无平台后端时 `bm_hal_cpu_id()` 恒为 0，与 `BM_CONFIG_CPU_COUNT==1` 等价。
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
#include "hal/bm_hal_cpu.h"

void bm_hal_cpu_init(void) {
}

uint32_t bm_hal_cpu_id(void) {
    return 0u;
}

int bm_hal_cpu_is_bootstrap(void) {
    return 1;
}

int bm_hal_cpu_boot_secondary(uintptr_t entry_pc) {
    (void)entry_pc;
    return BM_ERR_NOT_SUPPORTED;
}

int bm_hal_cpu_join_secondary(void) {
    return BM_OK;
}

void bm_hal_cpu_yield(void) {
}
