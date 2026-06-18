/**
 * @file bm_hrt.h
 * @brief 高分辨率定时（HRT）调度器
 *
 * 管理微秒级周期 slot。当前调度器仅实现 BM_HRT_TRIGGER_TIMER；
 * PWM/ADC 硬件槽通过 HAL bind 路径（见 bm_exec）接入。
 *
 * @core_affinity 本核（per-CPU）
 * 每核独立 HRT 定时器与槽表，bm_hrt_init/start/stop 仅影响调用者所在 CPU。
 * 不同核的 HRT 周期彼此独立，通过 bm_sync 进行硬件级相位对齐。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-10
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 *
 */
#ifndef BM_HRT_H
#define BM_HRT_H

#include "hal/bm_hal_hrt.h"
#include "bm/common/bm_types.h"

/** 硬件触发源类型（调度器当前仅处理 TIMER） */
typedef enum {
    BM_HRT_TRIGGER_TIMER,
    BM_HRT_TRIGGER_PWM_UPDATE,    /**< 预留：由 HAL bind 实现 */
    BM_HRT_TRIGGER_ADC_COMPLETE   /**< 预留：由 HAL bind 实现 */
} bm_hrt_trigger_t;

/** HRT 调度 slot 描述 */
typedef struct {
    uint32_t period_us;
    bm_hrt_trigger_t trigger;
    bm_hrt_callback_t callback;
    void *context;
    const char *name;
} bm_hrt_slot_t;

/**
 * @brief 初始化 HRT 调度器并注册 slot 表
 *
 * @param slots slot 描述数组
 * @param slot_count slot 数量
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；其他为平台错误码
 */
int bm_hrt_init(const bm_hrt_slot_t *slots, uint32_t slot_count);

/**
 * @brief 启动 HRT 调度
 *
 * `bm_hrt_init(NULL, 0)` 是合法的零槽初始化；零槽时 start 返回 BM_OK，
 * 但 `bm_hrt_is_started()` 仍为 0（调度器视为未运行）。
 *
 * @return BM_OK 成功；BM_ERR_NOT_INIT 未初始化；其他为平台错误码
 */
int bm_hrt_start(void);

typedef int (*bm_hrt_start_gate_t)(void);

/**
 * @brief Install a start gate checked before enabling the timer IRQ.
 */
void bm_hrt_set_start_gate(bm_hrt_start_gate_t gate);

/**
 * @brief 停止 HRT 调度
 */
void bm_hrt_stop(void);

/**
 * @brief 重置 HRT 调度器状态
 */
void bm_hrt_reset(void);

/**
 * @brief 错过 deadline 时的弱符号钩子（可覆盖）
 *
 * 默认实现为空。钩子在定时器 ISR 中执行，覆盖实现必须有界且不得记录日志。
 * 不支持弱符号的平台可定义 `BM_CONFIG_HRT_EXTERNAL_DEADLINE_HOOK=1`
 * 并由应用提供该函数。
 *
 * @param slot 触发 miss 的 slot 描述指针
 */
void bm_hrt_deadline_missed_hook(const bm_hrt_slot_t *slot);

/**
 * @brief 校验 period_us 是否为合法 HRT 定时器周期
 *
 * @param period_us 周期（微秒）
 * @return BM_OK 有效；BM_ERR_INVALID 无效或换算后超过 INT32_MAX tick
 */
int bm_hrt_validate_period_us(uint32_t period_us);

/**
 * @brief 查询指定槽累计 deadline 错过次数
 *
 * @param slot_index 槽索引
 * @return 错过次数；索引无效返回 0
 */
uint32_t bm_hrt_get_deadline_missed(uint32_t slot_index);

/**
 * @brief 查询全部槽累计 deadline 错过次数之和
 *
 * @return 各槽 `bm_hrt_get_deadline_missed` 之和
 */
uint32_t bm_hrt_get_deadline_missed_total(void);

/**
 * @brief 查询 HRT 调度器是否已启动（只读）
 *
 * 零槽 start 成功时仍返回 0。
 *
 * @return 1 已启动；0 未启动、未 init 或零槽
 */
int bm_hrt_is_started(void);

/**
 * @brief 协作式分派到期 HRT 槽（非 ISR 上下文可调用）
 *
 * QEMU 等慢仿真环境下，主循环可显式调用以补充定时器 IRQ 调度。
 */
void bm_hrt_poll(void);

#endif /* BM_HRT_H */
