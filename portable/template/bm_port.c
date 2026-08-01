/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_port.c
 * @brief Bmelod Port 组合模板 — arch 层 + vendor 弱钩子
 *
 * 复制到应用工程，对接厂商 HAL。文档：docs/04-移植与IDE集成/03-Port移植层bm_port.md
 *
 * 集成要点：
 * 1. **不要**在本文件定义 `bm_drv_critical_api` / `bm_drv_memory_api` — 由
 *    `portable/arch/<id>/`（`bm_arch_drv_bundle.c`）提供。
 * 2. 将 `portable/arch/<id>` 加入 include path，以便使用 `bm_arch_portmacro.h`。
 * 3. 下方 timer/wdg 为弱符号示例、uart 为默认控制台设备示例（统一实例模型）；
 *    应用可提供强符号覆盖，或改用 vendor 导出设备。
 * 4. 使用 `BM_BACKEND` pack 时通常无需本文件（pack 已链 arch + vendor）。
 * 5. 若定时器 ISR（`bm_port_timer_isr`）派发的用户回调可能包含浮点运算
 *    （例如电流环等控制律回调），必须在派发前后调用 `portable/arch/<id>/
 *    bm_arch_isr_fpu.h` 提供的 `bm_arch_isr_fpu_enter/exit` 守卫——多数裸机
 *    ISR 入口默认不保存/不开启浮点协处理器现场，中断内直接跑浮点会踩坑
 *    （轻则现场污染，重则触发协处理器异常复位，详见该头文件各架构平台
 *    真相注释）。见 `bm_port_timer_isr` 内的示范。
 * 6. Hardware HRT 端口契约：任何会在 HRT 优先级派发框架回调的厂商 IRQ
 *    handler（ADC/PWM 完成、DMA 完成等，即不经 hrt_dispatch 的直驱链路），
 *    必须在回调派发首尾成对调用 `bm/common/bm_critical_wrap.h` 的
 *    `bm_hrt_isr_enter/exit`——两模式（掩码与非掩码）下 event/ultra/mempool
 *    的 fail-closed 拦截均依赖该上下文标记；漏接则 HRT 误调 SRT 队列 API
 *    会 fail-open。参考实现：
 *    `portable/vendor/stm32g4/bm_vendor_adc_stm32g4.c` 的 ADC1_2_IRQHandler。
 * 7. 实例出口：新后端须在后端 include 目录提供 `bm_hal_devices_<backend>.h`
 *    （`#include` 既有实例头聚合声明 + `bm_<class>_default` 首选实例别名，
 *    某类无实例则不定义别名），并在对应 pack 向应用编译单元 PUBLIC 注入
 *    `BM_HAL_DEVICES_HEADER="bm_hal_devices_<backend>.h"`；应用只 include
 *    `hal/bm_hal_devices.h`。参考 `portable/sim/native/bm_hal_devices_native.h`
 *    与 `docs/03-移植与IDE集成/01-HAL契约与移植要点.md` 实例出口约定一节。
 * 8. NVS 后端：凡编入 NVS 实现的 pack，必须向已创建的 `bm_hal` 目标
 *    PRIVATE 注入 `BM_DRV_HAS_NVS_BACKEND`（backend 的 PUBLIC 定义不会
 *    逆向传播到 bm_hal 编译单元）——该宏同时是 `bm_persist.c` 编译期门与
 *    `Source/hal/bm_hal_nvs.c` fail-closed 桩的守卫，漏注入会导致
 *    persist 裁剪而桩生效（返回 BM_ERR_NOT_INIT）的口径分叉。参考
 *    `portable/packs/native_sim/CMakeLists.txt`。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 2.4
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-15       2.0            zeh            组合模板：arch 头 + vendor 弱钩子
 * 2026-06-15       2.1            zeh            修正弱符号覆盖点为全局 API 对象
 * 2026-07-11       2.2            zeh            timer_isr 补 ISR FPU 守卫调用示范（bm_arch_isr_fpu.h）
 * 2026-07-31       2.3            zeh            补 Hardware HRT 端口 bm_hrt_isr_enter/exit
 *                                                接线契约说明（要点 6 与示范代码）
 * 2026-08-01       2.4            zeh            补实例出口（devices 头 + pack 宏注入）
 *                                                与 NVS pack 宏注入两条落地清单
 *                                                （要点 7、8）
 * 2026-08-01       2.4            zeh           补齐 Doxygen 合规元数据
 *
 */
#include <stddef.h>
#include <stdint.h>

#include "bm_drv_timer.h"
#include "bm_drv_uart.h"
#include "bm_hal_uart.h"
#include "bm_drv_wdg.h"
#include "bm_types.h"

/*
 * 将 portable/arch/<id> 加入 include path 后取消下行注释，例如 armv7em：
 * #include "bm_arch_portmacro.h"
 */

#if defined(__GNUC__) || defined(__clang__)
#define BM_PORT_WEAK __attribute__((weak))
#else
#define BM_PORT_WEAK
#endif

static void (*g_tick_cb)(void);
static uint32_t g_tick_hz;

/**
 * @brief 初始化模板定时器频率
 *
 * @param freq_hz 定时器频率，单位为 Hz
 * @return BM_OK 初始化成功
 */
static int port_timer_init(uint32_t freq_hz) {
    g_tick_hz = freq_hz;
    return BM_OK;
}

/**
 * @brief 停止模板定时器并清除回调
 */
static void port_timer_stop(void) {
    g_tick_cb = NULL;
}

/**
 * @brief 读取模板定时器计数
 *
 * @return 当前计数；模板桩固定返回 0
 */
static uint32_t port_timer_get_ticks(void) {
    return 0u;
}

/**
 * @brief 读取模板定时器频率
 *
 * @return 已配置频率，单位为 Hz
 */
static uint32_t port_timer_get_freq(void) {
    return g_tick_hz;
}

/**
 * @brief 设置模板定时器回调
 *
 * @param cb 定时器回调；可为 NULL
 */
static void port_timer_set_callback(void (*cb)(void)) {
    g_tick_cb = cb;
}

BM_PORT_WEAK const struct bm_timer_driver_api bm_drv_timer_api = {
    port_timer_init,
    port_timer_stop,
    port_timer_get_ticks,
    port_timer_get_freq,
    port_timer_set_callback,
};

/**
 * @brief 应用定时器 ISR 中调用，转发框架 tick 回调。
 *
 * @note 若 g_tick_cb 链路可能触达浮点回调（如控制律），须在派发前后加
 *       ISR FPU 守卫。将 `portable/arch/<id>` 加入 include path 后取消下方
 *       注释即可接线（no-op 架构上零开销，接线不影响行为）：
 * @code
 * #include "<archid>/bm_arch_isr_fpu.h"
 * static uint8_t g_fpu_sa[BM_ARCH_ISR_FPU_SA_SIZE] __attribute__((aligned(16)));
 * void bm_port_timer_isr(void) {
 *     unsigned prev = bm_arch_isr_fpu_enter(g_fpu_sa);
 *     if (g_tick_cb) {
 *         g_tick_cb();
 *     }
 *     bm_arch_isr_fpu_exit(g_fpu_sa, prev);
 * }
 * @endcode
 *
 * @note Hardware HRT 端口（不经 hrt_dispatch 的厂商 IRQ 直驱回调链路）
 *       还须成对调用 bm_hrt_isr_enter/exit 标记 HRT ISR 上下文，示范：
 * @code
 * #include "bm_critical_wrap.h"
 * void ADC_IRQHandler(void) {
 *     ... 清标志 ...
 *     bm_hrt_isr_enter();
 *     if (complete_cb) {
 *         complete_cb(user);
 *     }
 *     bm_hrt_isr_exit();
 * }
 * @endcode
 */
void bm_port_timer_isr(void) {
    if (g_tick_cb) {
        g_tick_cb();
    }
}

/**
 * @brief 初始化模板 UART 设备
 *
 * @param dev UART 设备
 * @param config 平台配置
 * @return BM_OK 初始化成功
 */
static int port_uart_init(const struct bm_hal_uart *dev, void *config) {
    (void)dev;
    (void)config;
    return BM_OK;
}

/**
 * @brief 通过模板 UART 发送数据
 *
 * @param dev UART 设备
 * @param data 待发送数据
 * @param len 数据长度
 * @return BM_OK 模板桩接受发送请求
 */
static int port_uart_send(const struct bm_hal_uart *dev,
                          const uint8_t *data, size_t len) {
    (void)dev;
    (void)data;
    (void)len;
    return BM_OK;
}

/**
 * @brief 从模板 UART 接收数据
 *
 * @param dev UART 设备
 * @param data 接收缓冲区
 * @param max_len 缓冲区容量
 * @return 实际接收字节数；模板桩固定返回 0
 */
static size_t port_uart_recv(const struct bm_hal_uart *dev,
                             uint8_t *data, size_t max_len) {
    (void)dev;
    (void)data;
    (void)max_len;
    return 0u;
}

/**
 * @brief 设置模板 UART 接收回调
 *
 * @param dev UART 设备
 * @param cb 单字节接收回调；可为 NULL
 */
static void port_uart_set_rx_callback(const struct bm_hal_uart *dev,
                                      void (*cb)(uint8_t c)) {
    (void)dev;
    (void)cb;
}

static const struct bm_uart_driver_api g_port_uart_api = {
    port_uart_init,
    port_uart_send,
    port_uart_recv,
    port_uart_set_rx_callback,
};

/** @brief 默认控制台 UART 设备（统一实例模型；应用可提供强符号覆盖）。 */
BM_PORT_WEAK const bm_hal_uart_t bm_uart_default = { &g_port_uart_api, NULL };

/**
 * @brief 初始化模板看门狗
 *
 * @param timeout_ms 超时时间，单位为毫秒
 * @return BM_OK 初始化成功
 */
static int port_wdg_init(uint32_t timeout_ms) {
    (void)timeout_ms;
    return BM_OK;
}

/**
 * @brief 喂模板看门狗
 */
static void port_wdg_feed(void) {
}

BM_PORT_WEAK const struct bm_wdg_driver_api bm_drv_wdg_api = {
    port_wdg_init,
    port_wdg_feed,
};
