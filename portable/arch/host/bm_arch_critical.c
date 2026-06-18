/**
 * @file bm_arch_critical.c
 * @brief 宿主架构临界区（与 stub 相同语义，供单核 PC 测试）
 *
 * native_sim 的按 CPU 路由仿真使用 sim/native 专用临界区实现。
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
#include "port/bm_arch_ops.h"
#include "bm/common/bm_atomic_ipc.h"

bm_irq_state_t bm_arch_critical_enter(void) {
    bm_atomic_ipc_fence_full();
    return 0;
}

void bm_arch_critical_exit(bm_irq_state_t state) {
    (void)state;
    bm_atomic_ipc_fence_full();
}

int bm_arch_in_isr(void) {
    return 0;
}

#if BM_HAL_HAS_PRIORITY_MASK
bm_irq_state_t bm_arch_critical_enter_below(uint8_t threshold) {
    (void)threshold;
    return bm_arch_critical_enter();
}

void bm_arch_critical_exit_below(bm_irq_state_t previous_state) {
    bm_arch_critical_exit(previous_state);
}
#endif
