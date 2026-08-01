/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_arch_isr_fpu.h
 * @brief Xtensa 架构 ISR 内安全使用 FPU 的协处理器(CP0) 守卫。
 *
 * ESP Xtensa 核心在中断上下文默认禁用 FPU(CP0)。若 ISR 内执行浮点运算（例如
 * 绑到定时器/MCPWM ISR 的电流环回调），会触发 Coprocessor 异常；即便临时开启
 * CP0，也必须先保存被打断代码的 FPU 现场、运算后再恢复，否则会破坏被抢占代码
 * 的浮点状态。本头把这套守卫封装成可复用 inline，让任何绑到 ISR 的浮点回调
 * 透明地获得 FPU 安全，调用方不再直接接触 xthal_*。
 *
 * @par 平台门控（BM_ARCH_XTENSA_HAS_XTHAL）:
 *   真实实现仅在 `BM_ARCH_XTENSA_HAS_XTHAL` 下编译：xthal_* API
 *   （xtensa/hal.h）与 XCHAL_CP0_SA_SIZE（xtensa/config/core.h）均由 IDF
 *   xtensa 组件提供，链接依赖见 portable/vendor/esp32_idf/CMakeLists.txt
 *   （libxt_hal.a）。该宏由**明确知道 xthal 头与库可用**的构建目标显式定义
 *   （当前唯一定义方：vendor/esp32_idf 目标的 target_compile_definitions），
 *   独立裸机/QEMU/compilecheck 构建不定义，enter/exit 退化为 no-op
 *   （见下条平台真相）。
 *
 * @attention 教训（2026-07-11 真机回归）：本头初版误用环境宏 `ESP_PLATFORM`
 *   做门控——该宏只在 idf.py 组件编译上下文里被 IDF 构建系统注入，vendor
 *   静态库经 pack 的独立 CMake 路径编译时**并不定义**，导致守卫整段被预处理
 *   成 no-op、真机 tick/PWM ISR 双双失去 CP0 保护（Coprocessor exception
 *   复现）。门控宏必须由知道 xthal 可用性的构建目标显式给出，绝不能依赖
 *   构建环境宏是否恰好存在；改动此门控后必须用 objdump 反汇编确认 ISR 中
 *   存在 cpenable/xthal 指令（反汇编硬门）。
 *
 * @par QEMU esp32 裸机平台真相:
 *   QEMU esp32 裸机向量（boot/qemu_esp32_smp/vectors_qemu_esp32_smp.S 的
 *   `_qemu_esp32_user_exc`）不保存浮点现场、CPENABLE 亦未开启：入口只读
 *   EXCCAUSE/INTENABLE 判定后直接调用 `qemu_esp32_smp_on_timer_irq`，全程
 *   未涉及 CP0。故该路径下 ISR 回调禁止浮点运算；本头在此路径下 enter/exit
 *   仅作接线占位（no-op），为未来若要补全 QEMU 侧 CP0 现场保存预留统一入口
 *   ——一旦实现，所有既有调用方无需改动即可透明获得真实保护。
 *
 * @par 跨芯片条件编译（BM_ARCH_XTENSA_HAS_XTHAL 内）:
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
 * @note 保存区由调用方提供，须 16 字节对齐、大小为 BM_ARCH_ISR_FPU_SA_SIZE；
 *       建议每个 ISR 上下文各持一份，避免共享/嵌套覆盖。本守卫不分配、不
 *       打印、不加锁，可在 IRAM ISR 内调用。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-11
 *
 * @par 修改日志:
 * 2026-08-01       1.2            Codex           补齐 Doxygen 合规元数据
 *
 *    Date         Version        Author          Description
 * 2026-07-11       1.0            zeh    从 vendor/esp32_idf/bm_vendor_esp32_isr_fpu.h 下沉为 arch 层原语
 * 2026-07-11       1.1            zeh    门控修正：ESP_PLATFORM（环境宏，vendor 静态库 TU 不定义→守卫被 no-op 化）→ BM_ARCH_XTENSA_HAS_XTHAL（构建目标显式定义），修真机 Coprocessor exception 回归
 * 2026-07-11       1.2            zeh    enter/exit 加 always_inline：-Og 下独立拷贝落 .flash.text，IRAM ISR（ESP_INTR_FLAG_IRAM）调 flash 代码有 cache 关闭期崩溃风险
 */
#ifndef BM_ARCH_ISR_FPU_H
#define BM_ARCH_ISR_FPU_H

#ifdef __cplusplus
extern "C" {
#endif

#if defined(BM_ARCH_XTENSA_HAS_XTHAL)

/* xthal_get/set_cpenable、xthal_save/restore_cp0。 */
#include "xtensa/hal.h"
/* XCHAL_CP0_SA_SIZE（CP0/FPU 现场保存区字节数）。该宏定义在
 * xtensa/config/core.h（其内含 config/tie.h），不在 core-isa.h，故用 core.h。 */
#include "xtensa/config/core.h"

/** @brief CPENABLE 中 CP0(FPU) 对应位（bit0）。 */
#define BM_ARCH_ISR_FPU_CP0_BIT  0x1u

/**
 * @brief CP0(FPU) 现场保存区字节数。
 *
 * 有 FPU（XCHAL_CP0_SA_SIZE > 0，如 ESP32/S3=72）时取权威宏；无 FPU
 * （XCHAL_CP0_SA_SIZE == 0，如 ESP32-S2）时取 1，作为占位以避免零长数组。
 */
#if XCHAL_CP0_SA_SIZE > 0
#define BM_ARCH_ISR_FPU_SA_SIZE  XCHAL_CP0_SA_SIZE
#else
#define BM_ARCH_ISR_FPU_SA_SIZE  1
#endif

#else /* !BM_ARCH_XTENSA_HAS_XTHAL：独立裸机/QEMU esp32/compilecheck，无 xthal_*，
       * 见文件头「QEMU esp32 裸机平台真相」与「教训」注释 */

/** @brief 无 xthal 路径无现场可存，占位大小为 1（避免零长数组）。 */
#define BM_ARCH_ISR_FPU_SA_SIZE  1

#endif /* BM_ARCH_XTENSA_HAS_XTHAL */

/**
 * @brief 进入 ISR 浮点临界区：开启 CP0 并保存被打断现场。
 *
 * 顺序：读 CPENABLE → 置 CP0 位 → 保存 CP0 现场到 @p sa。定义了
 * BM_ARCH_XTENSA_HAS_XTHAL 且有 FPU 时执行真实存盘；其余情况（无 xthal
 * 环境，或无 FPU 芯片）退化为 no-op 并返回 0。返回值须原样传给配对的 exit。
 *
 * @note 强制内联（always_inline）：调用方多为 IRAM_ATTR ISR（esp_intr_alloc
 *       带 ESP_INTR_FLAG_IRAM），若 -Og 下不内联，本函数会以独立拷贝落在
 *       .flash.text——cache 关闭期间从 IRAM ISR 调 flash 代码会崩。强制内联
 *       使守卫指令直接嵌入 ISR 本体（也便于 objdump 反汇编硬门直接看到
 *       cpenable/xthal 指令）。
 *
 * @param[out] sa CP0 现场保存区，须 16 字节对齐、大小 BM_ARCH_ISR_FPU_SA_SIZE。
 * @return 进入前的 CPENABLE 值，供 exit 还原。
 */
__attribute__((always_inline)) static inline unsigned bm_arch_isr_fpu_enter(void *sa)
{
#if defined(BM_ARCH_XTENSA_HAS_XTHAL) && XCHAL_CP0_SA_SIZE > 0
    unsigned prev = xthal_get_cpenable();
    xthal_set_cpenable(prev | BM_ARCH_ISR_FPU_CP0_BIT);
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
 * 顺序：从 @p sa 恢复 CP0 现场 → 还原 CPENABLE 到 @p prev。定义了
 * BM_ARCH_XTENSA_HAS_XTHAL 且有 FPU 时执行真实恢复；其余情况退化为 no-op。
 * @p sa 必须与配对 enter 同一缓冲，@p prev 必须为该 enter 的返回值。
 *
 * @note 强制内联理由同 bm_arch_isr_fpu_enter（IRAM ISR 安全 + 反汇编硬门）。
 *
 * @param[in] sa   CP0 现场保存区（与配对 enter 同一缓冲）。
 * @param[in] prev 配对 enter 返回的 CPENABLE 旧值。
 */
__attribute__((always_inline)) static inline void bm_arch_isr_fpu_exit(void *sa, unsigned prev)
{
#if defined(BM_ARCH_XTENSA_HAS_XTHAL) && XCHAL_CP0_SA_SIZE > 0
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

#endif /* BM_ARCH_ISR_FPU_H */
