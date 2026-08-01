/**
 * @file bm_exec.h
 * @brief 确定性执行实例：IRQ-Step、Periodic、Block/Frame RT
 *
 * 执行实例承载状态、配置、资源声明、生命周期与多槽位调度契约。核心仅区分
 * Hardware/Periodic/Block/Frame 槽语义；具体外设或 DMA 块源由 HAL bind 或
 * bm_stream 适配器连接。详见 docs/06-路线图与多领域/02-多领域确定性流式架构.md。
 *
 * @core_affinity owner_cpu 约束
 * 每个 bm_exec 实例绑定到 owner_cpu，init/start/stop/drain 仅可在该核上调用。
 * Block/Frame 槽的 bm_stream 须与 exec 实例在同一核上。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 2.1
 * @date 2026-06-12
 *
 * @par 修改日志:
 * 2026-08-01       2.1            Codex           补齐 Doxygen 合规元数据
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-12       2.0            zeh            领域中性 bm_exec
 * 2026-06-12       2.1            zeh            Block/Frame 槽与 bm_stream
 * 2026-06-13       2.2            zeh            Block 槽 deadline 错过钩子
 * 2026-06-14       2.3            zeh            owner_cpu；prepare/irq_release/drain
 * 2026-06-14       2.4            zeh            deadline miss 可注册处理函数
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_EXEC_H
#define BM_EXEC_H

#include "hal/bm_hal_hrt.h"
#include "bm/hybrid/bm_resource.h"
#include "bm/common/bm_types.h"

#include "bm/hybrid/bm_block.h"
#include "bm/hybrid/bm_stream.h"

typedef struct bm_exec bm_exec_t;

typedef enum {
    BM_EXEC_SESSION_NONE = 0,
    BM_EXEC_SESSION_INITED,
    BM_EXEC_SESSION_STARTED,
    BM_EXEC_SESSION_STOPPING
} bm_exec_session_t;

typedef void (*bm_exec_run_fn_t)(const bm_exec_t *instance);
typedef void (*bm_exec_block_fn_t)(const bm_exec_t *instance, bm_block_t *block);

typedef enum {
    BM_EXEC_SLOT_HARDWARE,
    BM_EXEC_SLOT_PERIODIC,
    BM_EXEC_SLOT_BLOCK,
    BM_EXEC_SLOT_FRAME
} bm_exec_slot_kind_t;

#define BM_EXEC_SLOT_FLAG_FRAMEWORK_RELEASE  (1u << 0)

typedef struct {
    bm_exec_slot_kind_t kind;
    uint32_t period_us;
    uint32_t deadline_us;
    uint32_t flags;
    bm_exec_run_fn_t run;
    bm_exec_block_fn_t run_block;
    int (*bind)(const bm_exec_t *instance,
                const bm_hal_hrt_binding_t *binding);
    bm_stream_t *stream;
    const char *name;
} bm_exec_slot_t;

typedef struct {
    int (*init)(const bm_exec_t *instance);
    int (*start)(const bm_exec_t *instance);
    void (*safe_stop)(const bm_exec_t *instance);
} bm_exec_ops_t;

struct bm_exec {
    uint32_t id;
    uint8_t owner_cpu;
    const char *name;
    void *state;
    const void *config;
    const void *resources;
    const bm_exec_slot_t *slots;
    uint32_t slot_count;
    const bm_resource_claim_t *claims;
    uint32_t claim_count;
    const bm_exec_ops_t *ops;
};

/**
 * @brief 批量初始化执行实例并建立资源与硬件绑定
 * @param instances 实例指针数组
 * @param count 实例数量
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；其他为阶段错误码
 */
int bm_exec_init_all(const bm_exec_t *const *instances, uint32_t count);
/**
 * @brief 批量启动已初始化的执行实例
 * @param instances 实例指针数组，须与初始化时一致
 * @param count 实例数量
 * @return BM_OK 成功；BM_ERR_INVALID 参数或会话不匹配；其他为启动错误码
 */
int bm_exec_start_all(const bm_exec_t *const *instances, uint32_t count);
/**
 * @brief 安全停止全部实例并释放 HRT 与硬件绑定
 * @param instances 实例指针数组；NULL 时使用内部记录
 * @param count 实例数量
 */
void bm_exec_safe_stop_all(const bm_exec_t *const *instances, uint32_t count);

/**
 * @brief 过滤并初始化归属当前 CPU 的执行实例
 * @param instances 全局实例指针数组
 * @param count 实例数量
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；其他为初始化错误码
 */
int bm_exec_init_on_this_cpu(const bm_exec_t *const *instances, uint32_t count);
/**
 * @brief 过滤并启动归属当前 CPU 的执行实例
 * @param instances 全局实例指针数组
 * @param count 实例数量
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；其他为启动错误码
 */
int bm_exec_prepare_on_this_cpu(const bm_exec_t *const *instances, uint32_t count);
/**
 * @brief 释放全部 IRQ 资源并启动 HRT
 * @return BM_OK 成功；BM_ERR_INVALID 或 BM_ERR_NOT_INIT 表示状态非法；其他为平台错误码
 */
int bm_exec_irq_release_all(void);
/**
 * @brief 在当前 CPU 通过门控后释放 IRQ 资源
 * @return BM_OK 成功；BM_ERR_INVALID CPU 无效；BM_ERR_NOT_INIT 门控拒绝；其他为平台错误码
 */
int bm_exec_irq_release_on_this_cpu(void);
/**
 * @brief 按预算处理当前 CPU 的 Block/Frame 流
 * @param budget 本次最多消费的块数
 * @return 实际消费块数；未启动或在 ISR 中返回 0
 */
int bm_exec_drain_streams(uint32_t budget);

typedef int (*bm_exec_irq_release_gate_t)(void);
/**
 * @brief 设置 IRQ 释放前的门控回调
 * @param gate 门控回调；NULL 表示清除
 */
void bm_exec_set_irq_release_gate(bm_exec_irq_release_gate_t gate);

typedef void (*bm_exec_deadline_miss_fn_t)(const bm_exec_slot_t *slot,
                                           bm_block_t *block,
                                           uint32_t elapsed_us);

/**
 * @brief 注册 Block/Frame deadline miss 处理（优先于弱符号钩子）
 *
 * @param fn 处理函数；NULL 恢复为仅调用 `bm_exec_block_deadline_missed_hook`
 */
void bm_exec_set_deadline_miss_handler(bm_exec_deadline_miss_fn_t fn);

/**
 * @brief 按 ID 查找执行实例
 * @param instances 实例指针数组
 * @param count 实例数量
 * @param id 目标实例 ID
 * @return 匹配实例指针；参数无效或未找到时返回 NULL
 */
const bm_exec_t *bm_exec_find(const bm_exec_t *const *instances,
                              uint32_t count,
                              uint32_t id);

/**
 * @brief 查询当前 CPU 的执行会话状态
 * @return 当前会话状态；CPU 状态不可用时返回 BM_EXEC_SESSION_NONE
 */
bm_exec_session_t bm_exec_get_session(void);

/**
 * @brief Block/Frame 槽处理前已超过 deadline_us 时调用（弱符号，可覆盖）
 *
 * 默认实现为空。不支持弱符号的平台可定义 `BM_CONFIG_EXEC_EXTERNAL_DEADLINE_HOOK=1`
 * 并由应用提供该函数。
 *
 * @param slot 触发 deadline 错过的槽描述指针
 * @param block 待处理的块
 * @param elapsed_us 自块时间戳起已过的微秒数
 */
void bm_exec_block_deadline_missed_hook(const bm_exec_slot_t *slot,
                                        bm_block_t *block,
                                        uint32_t elapsed_us);

#endif /* BM_EXEC_H */
