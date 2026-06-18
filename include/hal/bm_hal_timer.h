/**
 * @file bm_hal_timer.h
 * @brief 系统定时器 HAL 接口
 *
 * 提供自由运行计数器、频率查询及周期性 tick 回调注册。
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-06-15
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            clock epoch bump API
 * 2026-06-15       1.2            zeh            非法 CPU 返回无效 timer 句柄
 * 2026-06-15       1.3            zeh            非法 CPU epoch 返回 UINT32_MAX
 *
 */
#ifndef BM_HAL_TIMER_H
#define BM_HAL_TIMER_H

#include <stdint.h>

/**
 * @brief 初始化系统定时器
 *
 * 驱动须按 `freq_hz` 产生周期性 tick；在 `set_callback` 注册有效回调之前
 * 不得触发回调（可先 `set_callback(NULL)` 再 init）。
 *
 * @param freq_hz 定时器计数频率（Hz）
 * @return BM_OK 成功；否则为平台错误码
 */
int bm_hal_timer_init(uint32_t freq_hz);

/**
 * @brief 停止系统定时器
 */
void bm_hal_timer_stop(void);

/**
 * @brief 读取当前 tick 计数值
 *
 * @return 自由运行计数器当前值
 */
uint32_t bm_hal_timer_get_ticks(void);

/**
 * @brief 查询定时器计数频率
 *
 * @return 计数频率（Hz）
 */
uint32_t bm_hal_timer_get_freq(void);

/**
 * @brief 注册定时器周期性 tick 回调
 *
 * 按 `init` 所设 `freq_hz` 每个计数节拍在 ISR 上下文调用一次；NULL 取消注册。
 *
 * @param cb tick 回调函数；NULL 表示取消注册
 */
void bm_hal_timer_set_callback(void (*cb)(void));

/**
 * @brief 返回逻辑 CPU 对应的单调时钟域 ID
 *
 * 每个 CPU 的 timer 逻辑与 `BM_TIMESTAMP_CLOCK_HRT` 兼容。
 *
 * @param cpu 逻辑 CPU 编号
 * @return 与该核 HRT 对齐的 clock_id；CPU 无效时返回 UINT16_MAX
 */
uint16_t bm_hal_timer_clock_id_for_cpu(uint32_t cpu);

/**
 * @brief 读取指定 CPU 的 tick 计数值
 *
 * 仅供监督、启动与测试路径；业务 deadline 比较须使用本核 `bm_hal_timer_get_ticks`。
 *
 * @param cpu 逻辑 CPU 编号
 * @return 该核自由运行计数器当前值
 */
uint32_t bm_hal_timer_get_ticks_on_cpu(uint32_t cpu);

/**
 * @brief 查询指定 CPU 的定时器计数频率
 *
 * @param cpu 逻辑 CPU 编号
 * @return 计数频率（Hz）；未知 CPU 或平台不支持时返回 0
 */
uint32_t bm_hal_timer_get_freq_on_cpu(uint32_t cpu);

/**
 * @brief 返回逻辑 CPU 当前时钟代际（profile 切换或局部复位时递增）
 *
 * @param cpu 逻辑 CPU 编号
 * @return 当前 clock epoch；CPU 无效时返回 UINT32_MAX
 */
uint32_t bm_hal_timer_clock_epoch_for_cpu(uint32_t cpu);

/**
 * @brief 返回绑定到指定 CPU 的定时器句柄（clock_id + epoch）
 *
 * CPU 无效时返回 `cpu=UINT8_MAX`、`clock_id=UINT16_MAX`、
 * `clock_epoch=UINT32_MAX` 的无效句柄。
 */
typedef struct {
    uint8_t  cpu;
    uint16_t clock_id;
    uint32_t clock_epoch;
} bm_hal_timer_handle_t;

bm_hal_timer_handle_t bm_hal_timer_for_cpu(uint32_t cpu);

/**
 * @brief 递增指定 CPU 的 clock epoch（profile 切换 / 局部复位）
 */
void bm_hal_timer_bump_clock_epoch(uint32_t cpu);

/**
 * @brief 递增所有逻辑 CPU 的 clock epoch
 */
void bm_hal_timer_bump_all_clock_epochs(void);

#endif /* BM_HAL_TIMER_H */
