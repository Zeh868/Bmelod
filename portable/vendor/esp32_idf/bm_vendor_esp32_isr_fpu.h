/**
 * @file bm_vendor_esp32_isr_fpu.h
 * @brief ESP 中断内安全使用 FPU 的协处理器(CP0)守卫。
 *
 * ESP Xtensa 核心在中断上下文默认禁用 FPU(CP0)。若 ISR 内执行浮点运算（例如
 * 绑到 MCPWM TEZ 的 FOC current_step），会触发 Coprocessor 异常；即便临时开启
 * CP0，也必须先保存被打断代码的 FPU 现场、运算后再恢复，否则会破坏被抢占代码
 * 的浮点状态。本头把这套守卫封装成可复用 inline，让任何绑到 ISR 的浮点回调
 * 透明地获得 FPU 安全，调用方不再直接接触 xthal_*。
 *
 * @par 跨芯片条件编译:
 *   保存区大小取 XCHAL_CP0_SA_SIZE（CP0 现场字节数）：
 *     - ESP32 / ESP32-S3：有 FPU，XCHAL_CP0_SA_SIZE = 72；
 *     - ESP32-S2：无 FPU，XCHAL_CP0_SA_SIZE = 0。
 *   守卫按 `XCHAL_CP0_SA_SIZE > 0` 条件编译：有 FPU 才真正存/恢复 CP0；无 FPU
 *   时 enter/exit 退化为 no-op。一份代码覆盖所有 ESP Xtensa 芯片。
 *
 * @par 顺序铁律（不可乱）:
 *   enter：读 CPENABLE → 置 CP0 位 → 保存现场；
 *   exit ：恢复现场 → 还原 CPENABLE。
 *   即「开 CP0 → 存现场 → 跑浮点 → 复现场 → 还原 CPENABLE」。
 *
 * @note 保存区由调用方提供，须 16 字节对齐、大小为
 *       BM_VENDOR_ESP32_ISR_FPU_SA_SIZE；建议每个 ISR 上下文各持一份，避免
 *       共享/嵌套覆盖。本守卫不分配、不打印、不加锁，可在 IRAM ISR 内调用。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-21
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-21       1.0            zeh        新增 ESP ISR FPU(CP0) 协处理器守卫
 */
#ifndef BM_VENDOR_ESP32_ISR_FPU_H
#define BM_VENDOR_ESP32_ISR_FPU_H

/* xthal_get/set_cpenable、xthal_save/restore_cp0。 */
#include "xtensa/hal.h"
/* XCHAL_CP0_SA_SIZE（CP0/FPU 现场保存区字节数）。该宏定义在
 * xtensa/config/core.h（其内含 config/tie.h），不在 core-isa.h，故用 core.h。 */
#include "xtensa/config/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief CPENABLE 中 CP0(FPU) 对应位（bit0）。 */
#define BM_VENDOR_ESP32_ISR_FPU_CP0_BIT  0x1u

/**
 * @brief CP0(FPU) 现场保存区字节数。
 *
 * 有 FPU（XCHAL_CP0_SA_SIZE > 0，如 ESP32/S3=72）时取权威宏；无 FPU
 * （XCHAL_CP0_SA_SIZE == 0，如 ESP32-S2）时取 1，作为占位以避免零长数组。
 */
#if XCHAL_CP0_SA_SIZE > 0
#define BM_VENDOR_ESP32_ISR_FPU_SA_SIZE  XCHAL_CP0_SA_SIZE
#else
#define BM_VENDOR_ESP32_ISR_FPU_SA_SIZE  1
#endif

/**
 * @brief 进入 ISR 浮点临界区：开启 CP0 并保存被打断现场。
 *
 * 顺序：读 CPENABLE → 置 CP0 位 → 保存 CP0 现场到 @p sa。有 FPU 时执行真实
 * 存盘；无 FPU 时退化为 no-op 并返回 0。返回值须原样传给配对的 exit。
 *
 * @param[out] sa CP0 现场保存区，须 16 字节对齐、大小
 *                BM_VENDOR_ESP32_ISR_FPU_SA_SIZE。
 * @return 进入前的 CPENABLE 值，供 exit 还原。
 */
static inline unsigned bm_vendor_esp32_isr_fpu_enter(void *sa)
{
#if XCHAL_CP0_SA_SIZE > 0
    unsigned prev = xthal_get_cpenable();
    xthal_set_cpenable(prev | BM_VENDOR_ESP32_ISR_FPU_CP0_BIT);
    xthal_save_cp0(sa);
    return prev;
#else
    (void)sa;
    return 0u;
#endif
}

/**
 * @brief 退出 ISR 浮点临界区：恢复被打断现场并还原 CPENABLE。
 *
 * 顺序：从 @p sa 恢复 CP0 现场 → 还原 CPENABLE 到 @p prev。有 FPU 时执行真实
 * 恢复；无 FPU 时退化为 no-op。@p sa 必须与配对 enter 同一缓冲，@p prev 必须
 * 为该 enter 的返回值。
 *
 * @param[in] sa   CP0 现场保存区（与配对 enter 同一缓冲）。
 * @param[in] prev 配对 enter 返回的 CPENABLE 旧值。
 */
static inline void bm_vendor_esp32_isr_fpu_exit(void *sa, unsigned prev)
{
#if XCHAL_CP0_SA_SIZE > 0
    xthal_restore_cp0(sa);
    xthal_set_cpenable(prev);
#else
    (void)sa;
    (void)prev;
#endif
}

#ifdef __cplusplus
}
#endif

#endif /* BM_VENDOR_ESP32_ISR_FPU_H */
