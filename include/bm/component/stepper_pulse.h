/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file stepper_pulse.h
 * @brief STEP/DIR 脉冲步进驱动组件（芯片无关，resources 回调 + 平台定时器入口）
 *
 * 组件不直接依赖 GPIO/定时器契约：脉冲电平、方向电平与定时器武装全部经
 * resources 回调完成，平台定时器 ISR 调用 bm_stepper_pulse_on_timer()
 * 驱动步进——native_sim 可用假回调单测，实机由业务/vendor 绑一路 TIM
 * （定时器设备实例契约登记为已知缺口，不在本组件范围）。
 *
 * 行为模型：速度设定（steps/s，可来自 control_loop 输出）→ 半周期定时
 * 翻转 STEP，上升沿计步（有符号，含方向）；方向切换自动插入 DIR 建立
 * 时间；运行中反向切换先将 STEP 拉低，可选 dir_hold_us 保持后再改 DIR；
 * 脉冲频率上限经 config.max_step_rate_hz 钳制，min_high_us/min_low_us
 * 可强制最小脉宽；GPIO 回调非 BM_OK 时锁存 fault 并安全停机。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-07-28
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-27       1.0            zeh            新增（接口批 1 步进伺服栈）
 * 2026-07-28       1.1            zeh            dir_hold/min 脉宽/GPIO fault/en_set
 * 2026-07-28       1.2            zeh            set_enable 注明 fault 态允许断使能
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_STEPPER_PULSE_H
#define BM_STEPPER_PULSE_H

#include "bm/common/bm_types.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief stepper_pulse 配置（用户填写）
 *
 * E1 提醒：ISR 翻转方式建议 max_step_rate_hz ≤ 10kHz，更高频率的 CPU
 * 占用与时序抖动须实机评估（见 MATURITY.md）。
 *
 * dir_hold_us / min_high_us / min_low_us 为 0 时不施加额外约束（向后兼容）。
 */
typedef struct {
    uint32_t max_step_rate_hz; /**< 脉冲频率上限（steps/s），set_velocity 钳制 */
    uint32_t dir_setup_us;     /**< DIR 建立时间（µs），方向切换后插入 */
    uint32_t dir_hold_us;      /**< 方向切换前 STEP 低电平保持（µs）；0=不额外等待 */
    uint32_t min_high_us;      /**< STEP 高电平最小时间（µs）；0=仅用半周期 */
    uint32_t min_low_us;       /**< STEP 低电平最小时间（µs）；0=仅用半周期 */
} bm_stepper_pulse_config_t;

/** @brief 资源回调（平台绑定；user 统一透传） */
typedef struct {
    int  (*step_high)(void *user);                 /**< STEP 拉高；BM_OK 或 BM_ERR_* */
    int  (*step_low)(void *user);                  /**< STEP 拉低；BM_OK 或 BM_ERR_* */
    int  (*dir_set)(void *user, int level);        /**< DIR 电平（0/1）；BM_OK 或 BM_ERR_* */
    /**
     * @brief EN 使能脚电平（可选；NULL 表示无 EN 脚）
     *
     * 组件不自动拉 EN；由 App 经 bm_stepper_pulse_set_enable() 调用。
     */
    int  (*en_set)(void *user, int level);
    /**
     * @brief 请求下一次定时器到期不晚于 interval_us
     *
     * 语义为“到期时间上限”而非强制重设：平台已武装且剩余时间短于
     * interval_us 时保持不动；否则（未武装 / 剩余更长）按 interval_us
     * 重设。组件每次调速与每次 on_timer 都会调用——运行中加速可立即
     * 生效，同时避免每拍调速重置当前半周期导致脉冲发不出。
     *
     * @param interval_us 到期时间上限（µs）；0 = 取消定时器
     * @return BM_OK 成功；否则为平台错误码（组件按调度失败处理）
     */
    int  (*arm_timer)(void *user, uint32_t interval_us);
    void *user;
} bm_stepper_pulse_resources_t;

/** @brief stepper_pulse 状态（组件维护） */
typedef struct {
    int32_t  position;         /**< 步计数（有符号，上升沿 ±1） */
    float    velocity_sps;     /**< 当前速度设定（steps/s，含方向） */
    int      dir;              /**< 当前方向（+1/-1） */
    uint8_t  step_level;       /**< STEP 当前电平（0/1） */
    uint8_t  running;          /**< 步进中 */
    uint8_t  dir_wait_pending; /**< 方向建立等待槽未消费 */
    uint8_t  dir_hold_pending; /**< 方向切换前 hold 等待槽未消费 */
    uint8_t  fault;            /**< GPIO/定时器故障锁存（非 0 = 已停机） */
} bm_stepper_pulse_state_t;

/** @brief stepper_pulse 轴实例（四段式聚合，用户静态分配） */
typedef struct {
    bm_stepper_pulse_config_t    config;
    bm_stepper_pulse_resources_t resources;
    bm_stepper_pulse_state_t     state;
} bm_stepper_pulse_axis_t;

/**
 * @brief 校验配置（max_step_rate_hz > 0；min 脉宽与频率上限相容）
 * @param config 配置指针
 * @return BM_OK 合法；BM_ERR_INVALID 非法
 */
int bm_stepper_pulse_validate_config(const bm_stepper_pulse_config_t *config);

/**
 * @brief 初始化轴（状态清零，STEP 拉低，不定时不发脉冲）
 * @param axis 轴实例
 * @return BM_OK 成功；BM_ERR_INVALID 指针为空/配置非法/回调缺失
 */
int bm_stepper_pulse_init(bm_stepper_pulse_axis_t *axis);

/**
 * @brief 复位（位置/速度清零，清 fault，STEP 拉低，取消定时器）
 * @param axis 轴实例；NULL 静默返回
 */
void bm_stepper_pulse_reset(bm_stepper_pulse_axis_t *axis);

/**
 * @brief 清除 GPIO 故障锁存（不改动位置/速度；需 App 确认硬件已恢复）
 * @param axis 轴实例；NULL 静默返回
 */
void bm_stepper_pulse_clear_fault(bm_stepper_pulse_axis_t *axis);

/**
 * @brief 设定速度（steps/s，符号即方向；|v| 钳制到 max_step_rate_hz）
 *
 * v == 0 等价 stop。静止启动时立即 dir_set、插入 dir_setup_us
 * 建立槽并武装定时器；运行中同向调速只更新速度设定（不重置当前半周期，
 * 新速度在下一个 on_timer 生效）。运行中反向切换先将 STEP 拉低，再按
 * dir_hold_us（可为 0）等待后 dir_set + 建立槽。fault 锁存后本函数直接返回。
 *
 * @param axis 轴实例；NULL 静默返回
 * @param velocity_sps 目标速度（steps/s）
 */
void bm_stepper_pulse_set_velocity(bm_stepper_pulse_axis_t *axis,
                                   float velocity_sps);

/**
 * @brief 停止（速度归零，STEP 拉低，取消定时器；位置保持）
 * @param axis 轴实例；NULL 静默返回
 */
void bm_stepper_pulse_stop(bm_stepper_pulse_axis_t *axis);

/**
 * @brief 设置 EN 使能脚电平（resources.en_set 为 NULL 时不支持）
 *
 * GPIO 失败时锁存 fault 并停机。fault 锁存后仍允许断使能（enable==0，
 * 便于故障处置时先断开功率级），重新使能（enable!=0）返回 BM_ERR_IO。
 *
 * @param axis 轴实例
 * @param enable 非 0 使能，0 禁用
 * @return BM_OK 成功；BM_ERR_INVALID 指针为空；BM_ERR_NOT_SUPPORTED 无 en_set；
 *         BM_ERR_IO fault 锁存后尝试重新使能；其他 BM_ERR_* 为 GPIO 失败
 *         （已锁存 fault）
 */
int bm_stepper_pulse_set_enable(bm_stepper_pulse_axis_t *axis, int enable);

/**
 * @brief 读取当前步计数
 * @param axis 轴实例；NULL 返回 0
 * @return 步计数（有符号）
 */
int32_t bm_stepper_pulse_position(const bm_stepper_pulse_axis_t *axis);

/**
 * @brief 平台定时器到期入口（在定时器 ISR 上下文调用）
 *
 * 每次调用消费一个半周期：dir_hold / 方向建立槽未消费则先消费（不发脉冲），
 * 否则翻转 STEP（上升沿计步）并按当前速度重新武装下一次定时。
 *
 * @param axis 轴实例；NULL 静默返回
 */
void bm_stepper_pulse_on_timer(bm_stepper_pulse_axis_t *axis);

#ifdef __cplusplus
}
#endif

#endif /* BM_STEPPER_PULSE_H */
