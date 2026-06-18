/**
 * @file test_arch_critical.c
 * @brief 架构层临界区烟雾测试（交叉编译固件）
 *
 * 验证 `bm_drv_critical_api` enter/exit 嵌套与 `in_isr` 可链接。
 * RV64 在 QEMU virt 上通过 test finisher 退出（0x100000）。
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-06-15       1.1            zeh            QEMU virt finisher 退出
 *
 */
#include "bm_drv_critical.h"

#if defined(__riscv) && (__riscv_xlen == 64)
#define BM_ARCH_SMOKE_FINISHER_BASE 0x100000u
#define BM_ARCH_SMOKE_FINISHER_PASS 0x5555u
#define BM_ARCH_SMOKE_FINISHER_FAIL 0x3333u
#endif

volatile int g_arch_smoke_ok;

static void arch_smoke_finish(int ok) {
#if defined(__riscv) && (__riscv_xlen == 64)
    *(volatile unsigned int *)BM_ARCH_SMOKE_FINISHER_BASE =
        ok ? BM_ARCH_SMOKE_FINISHER_PASS : BM_ARCH_SMOKE_FINISHER_FAIL;
#else
    (void)ok;
#endif
}

int main(void) {
    bm_irq_state_t outer;
    bm_irq_state_t inner;

    g_arch_smoke_ok = 0;
    outer = bm_drv_critical_api.enter();
    inner = bm_drv_critical_api.enter();
    bm_drv_critical_api.exit(inner);
    bm_drv_critical_api.exit(outer);

    if (bm_drv_critical_api.in_isr() == 0) {
        g_arch_smoke_ok = 1;
    }

    arch_smoke_finish(g_arch_smoke_ok);

    for (;;) {
    }
    return 0;
}
