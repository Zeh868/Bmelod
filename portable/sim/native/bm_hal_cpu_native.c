/**
 * @file bm_hal_cpu_native.c
 * @brief native_sim CPU 抽象后端
 *
 * native_sim 仅模拟单核环境；从核启动返回不支持。
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

void bm_hal_cpu_yield(void) {
}
