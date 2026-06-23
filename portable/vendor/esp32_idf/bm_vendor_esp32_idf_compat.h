/**
 * @file bm_vendor_esp32_idf_compat.h
 * @brief ESP32-IDF vendor 兼容宏与局部退化定义
 *
 * 仅供 `portable/vendor/esp32_idf` 目录内部使用，不向公共头文件扩散。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            新增 vendor 私有 I/O 错误兼容宏
 * 2026-06-19       1.1            zeh            Phase 3：添加 PERIPH_RCC_ATOMIC 正规化宏
 *
 */
#ifndef BM_VENDOR_ESP32_IDF_COMPAT_H
#define BM_VENDOR_ESP32_IDF_COMPAT_H

#include "bm_types.h"

#ifndef BM_ERR_IO
/**
 * @brief 平台 I/O 错误兼容别名。
 *
 * 仅在 vendor 内部缺少显式 I/O 错误码时，退化映射到"不支持"。
 */
#define BM_ERR_IO BM_ERR_NOT_SUPPORTED
#endif

/*
 * PERIPH_RCC_ATOMIC 正规化封装宏（Phase 3）。
 *
 * IDF 5.2.3 机制说明：
 *   mcpwm_ll.h / timer_ll.h 将 mcpwm_ll_enable_bus_clock / timer_ll_enable_bus_clock
 *   重定义为宏，要求调用者在词法作用域内声明了 __DECLARE_RCC_ATOMIC_ENV /
 *   __DECLARE_RCC_RC_ATOMIC_ENV 变量（编译期守卫，防止在非临界区调用）：
 *     #define mcpwm_ll_enable_bus_clock(...) (void)__DECLARE_RCC_ATOMIC_ENV; ...
 *     #define timer_ll_enable_bus_clock(...) (void)__DECLARE_RCC_RC_ATOMIC_ENV; ...
 *
 * 真实 IDF 构建：PERIPH_RCC_ATOMIC() 宏（esp_private/periph_ctrl.h）在内部
 *   进入临界区并声明这些守卫变量，保证安全性。
 *
 * compilecheck / freestanding 路径：periph_ctrl.h 依赖 FreeRTOS，不可用。
 *   BM_PERIPH_RCC_ATOMIC_BEGIN 在退化路径下声明守卫变量，满足 IDF 宏的编译期检查，
 *   同时不引入 RTOS 临界区（init 阶段单线程，无并发风险）。
 *
 * 用法（仅在 init 阶段单线程环境中调用 ll_enable_bus_clock 的代码块）：
 *   BM_PERIPH_RCC_ATOMIC_BEGIN
 *       mcpwm_ll_enable_bus_clock(...);   // 同时使用两种守卫变量均可
 *       timer_ll_enable_bus_clock(...);
 *   BM_PERIPH_RCC_ATOMIC_END
 */
#if defined(ESP_PLATFORM) && !defined(BM_ESP32_COMPILECHECK_FFREESTANDING)
/**
 * @brief 真实 IDF 构建：进入 PERIPH_RCC_ATOMIC 临界块（来自 esp_private/periph_ctrl.h）。
 * @note  调用者需确保 periph_ctrl.h 已通过 CMake include 路径提供。
 */
#  define BM_PERIPH_RCC_ATOMIC_BEGIN  PERIPH_RCC_ATOMIC() {
/** @brief 真实 IDF 构建：退出 PERIPH_RCC_ATOMIC 临界块。 */
#  define BM_PERIPH_RCC_ATOMIC_END    }
#else
/**
 * @brief compilecheck/freestanding 退化：声明 IDF LL 宏所需的守卫变量，
 *        无临界区包装（init 阶段单线程，无并发风险）。
 *
 * 守卫变量说明：
 *   __DECLARE_RCC_ATOMIC_ENV   — mcpwm_ll.h / gpio_ll.h 等要求
 *   __DECLARE_RCC_RC_ATOMIC_ENV — timer_ll.h 等要求
 *
 * @note 不得在多线程或 ISR 上下文中使用退化路径。
 */
#  define BM_PERIPH_RCC_ATOMIC_BEGIN  { \
    int __DECLARE_RCC_ATOMIC_ENV = 0; (void)__DECLARE_RCC_ATOMIC_ENV; \
    int __DECLARE_RCC_RC_ATOMIC_ENV = 0; (void)__DECLARE_RCC_RC_ATOMIC_ENV;
/** @brief compilecheck/freestanding 退化：关闭守卫变量块。 */
#  define BM_PERIPH_RCC_ATOMIC_END    }
#endif /* ESP_PLATFORM && !BM_ESP32_COMPILECHECK_FFREESTANDING */

#endif /* BM_VENDOR_ESP32_IDF_COMPAT_H */
