/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_param.h
 * @brief 运行期参数注册表（批 P）：静态表 + RAM 镜像 + bm_persist 落盘
 *
 * 应用以静态 `bm_param_desc_t` 表登记可调参数（如平衡车 kp/ki），
 * 每项声明出厂默认值、热写落点（直写指针和/或 apply 回调）与可选的
 * persist 键。运行期通过 `bm_param_set/get` 读写 RAM 镜像；`save`/
 * `load_overlay` 负责与 `bm_persist` 之间的落盘/加载往返；`reset`
 * 一键恢复全表出厂默认。
 *
 * 典型用法：
 * @code
 * static const bm_param_desc_t k_params[] = {
 *     { "bal.kp", BAL_KP_DEFAULT, 0.0f, 0.0f, &g_bal_kp, NULL, NULL, "bal.kp", 0u },
 * };
 * bm_param_register_table(k_params, (uint16_t)(sizeof(k_params) / sizeof(k_params[0])));
 * bm_param_load_overlay();   // 上电：用持久化值覆盖出厂默认（如有）
 * ...
 * bm_param_set("bal.kp", 1.2f);
 * bm_param_save();
 * @endcode
 *
 * 值域校验规则（v1.1 新增，`bm_param_set`/`bm_param_load_overlay`/
 * `bm_param_register_table` 三处统一强制）：
 * 1. `isfinite(v)` 恒必需——NaN/Inf 一律拒绝，无逃生口；
 * 2. `min < max` 时按闭区间 `[min, max]` 校验；
 * 3. `min == max`（如都写 0）表示该参数无界，仅做 isfinite 校验
 *    （无界参数的显式逃生口，避免误填 0/0 被当成“只能取 0”）。
 *
 * @warning 并发契约：与 bm_persist 相同——本模块内部无任何并发保护
 *          （无锁、无临界区），全部 API 须在单一执行上下文（如 shell
 *          主循环）中调用，且不在 ISR 中。apply 回调落点跨执行上下文
 *          可见性依赖“4 字节对齐 float 单写具有原子性”这一假设，
 *          由登记方（app）的 apply 回调自行保证其落点满足该前提。
 *
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-11
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-11       1.0            zeh            正式发布（批 P：bm_param 参数注册表）
 * 2026-07-11       1.1            zeh            新增 min/max 值域校验（isfinite 恒必需，
 * 2026-08-01       1.1            zeh           补齐 Doxygen 合规元数据
 *                                                 min==max 为无界逃生口）
 *
 */
#ifndef BM_PARAM_H
#define BM_PARAM_H

#include "bm/common/bm_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 表项最大数量（超过登记时 register_table 返回 BM_ERR_NO_MEM） */
#ifndef BM_CONFIG_PARAM_MAX
#define BM_CONFIG_PARAM_MAX 16
#endif

#if BM_CONFIG_PARAM_MAX < 1 || BM_CONFIG_PARAM_MAX > 255
#error "BM_CONFIG_PARAM_MAX 须在 1..255 范围内"
#endif

/** 非热写标志：set 只改 RAM 镜像，需重启（boot overlay）才生效 */
#define BM_PARAM_FLAG_REBOOT  0x01u

/** bm_param_set 正值返回码：值已记录（RAM 镜像），重启后经 overlay 生效 */
#define BM_PARAM_REBOOT_REQUIRED 1

/**
 * @brief 参数热写应用回调
 *
 * @param val  新值
 * @param user 登记时提供的透传上下文（可为 NULL）
 */
typedef void (*bm_param_apply_fn_t)(float val, void *user);

/**
 * @brief reset 守卫回调
 *
 * @return 非 0 表示拒绝本次 reset（例如设备运行中不允许恢复出厂）
 */
typedef int (*bm_param_reset_guard_fn_t)(void);

/**
 * @brief save 守卫回调（语义同 reset guard）
 *
 * @return 非 0 表示拒绝本次 save（例如施力运行中不允许触发 NVS 落盘——
 *         flash 页擦除可达数十 ms，可能阻塞同核实时采样/控制路径）
 */
typedef int (*bm_param_save_guard_fn_t)(void);

/**
 * @brief 单条参数描述符（应用静态只读表，生存期覆盖整个运行期）
 */
typedef struct {
    const char         *name;       /**< 点分小写唯一名（如 "bal.kp"），静态生存期 */
    float               def_val;    /**< 出厂默认值（来源 gains.h 宏，保持单源） */
    float               min;        /**< 值域下界；min==max（如都写 0）= 不做区间校验，仅 isfinite */
    float               max;        /**< 值域上界（闭区间 [min,max]，仅 min<max 时生效） */
    float              *ptr;        /**< 直写目标；可 NULL（用 apply） */
    bm_param_apply_fn_t apply;      /**< 热写回调；可 NULL（用 ptr）；与 ptr 至少一个非空 */
    void               *apply_user; /**< apply 回调透传上下文 */
    const char         *pkey;       /**< persist 键；NULL = 不持久化 */
    uint8_t             flags;      /**< BM_PARAM_FLAG_REBOOT 等标志位 */
} bm_param_desc_t;

/**
 * @brief 登记参数静态表
 *
 * 校验表非空、1<=count<=BM_CONFIG_PARAM_MAX、每项 name 非空且
 * ptr/apply 至少一个非空，另逐项校验 `min`/`max` 均为有限值且
 * `min<=max`，以及 `def_val` 本身通过值域校验（挡表作者笔误：
 * def 越界或 min>max）。成功后 RAM 镜像逐项置为 def_val（不触发
 * apply——模块 init 已用同一 gains.h 宏灌过默认值）。可重复调用
 * （覆盖此前登记，供单测/重复 boot 使用），不影响已设置的 reset guard。
 *
 * @note 多个表项共享同一 `ptr` 落点属登记方错误用法，overlay/reset 等
 *       批量应用路径下的落点终值未定义，组件不做任何先后仲裁。
 *
 * @param table 静态描述符数组（调用方保证生存期覆盖后续所有 API 调用）
 * @param count 表项数
 * @return BM_OK 成功；BM_ERR_INVALID 参数非法（含 min/max 非有限、
 *         min>max 或 def_val 未过值域校验）；BM_ERR_NO_MEM 超出
 *         BM_CONFIG_PARAM_MAX
 */
int bm_param_register_table(const bm_param_desc_t *table, uint16_t count);

/**
 * @brief 从 persist 加载已保存值覆盖当前镜像（boot overlay）
 *
 * 逐项 pkey 非 NULL 者从 bm_persist 读取，命中且长度匹配则写入镜像
 * 并执行 ptr 直写/apply 回调（REBOOT 项此处照常 apply——boot 即为其
 * 重启生效点）。读回的值若未过值域校验（非 isfinite 或越界），视为
 * 坏 KV：跳过（不写镜像、不 apply、不计入返回条数，镜像保持出厂
 * 默认），并经 BM_LOGW 记录。
 *
 * @return 命中并应用的条数（>=0，坏值不计入）；未登记返回 BM_ERR_NOT_INIT
 */
int bm_param_load_overlay(void);

/**
 * @brief 按名设置参数值
 *
 * 未命中返回 BM_ERR_NOT_FOUND。命中后先做值域校验：`val` 非
 * isfinite 或越界（见值域校验规则）→ BM_ERR_INVALID，不写镜像、
 * 不 apply。校验通过才写 RAM 镜像：若该项带 BM_PARAM_FLAG_REBOOT，
 * 只改镜像，返回 BM_PARAM_REBOOT_REQUIRED；否则热写落点（先 ptr
 * 直写，后 apply 回调；两者都给则都执行），返回 BM_OK。
 *
 * @param name 参数名
 * @param val  新值
 * @return BM_OK / BM_PARAM_REBOOT_REQUIRED / BM_ERR_INVALID（值域拒绝）
 *         / 其他负错误码
 */
int bm_param_set(const char *name, float val);

/**
 * @brief 按名读取参数当前 RAM 镜像值
 *
 * @param name 参数名
 * @param out  输出值（非 NULL）
 * @return BM_OK 成功；BM_ERR_NOT_FOUND 未命中；BM_ERR_NOT_INIT 未登记
 */
int bm_param_get(const char *name, float *out);

/**
 * @brief 将全表当前镜像中带 pkey 的项写入 persist 并落盘
 *
 * save guard 非 NULL 且返回非 0 时拒绝执行（不触碰 persist），返回
 * BM_ERR_BUSY。否则逐项 pkey 非 NULL 者调用 bm_persist_set，任一失败
 * 立即返回该错误码；全部成功后调用 bm_persist_commit（失败返回其错误码）。
 * 全表无 pkey 项时直接返回 0，不触碰 persist（允许 persist 未 init 的
 * 纯 RAM 用法）。
 *
 * @return 落盘条数（>=0）或负错误码；BM_ERR_BUSY guard 拒绝
 */
int bm_param_save(void);

/**
 * @brief 全表恢复出厂默认
 *
 * guard 非 NULL 且返回非 0 时拒绝执行（不改动任何状态），返回
 * BM_ERR_BUSY。否则逐项 pkey 者调用 bm_persist_erase（BM_ERR_NOT_FOUND
 * 忽略）后 commit（全表无 pkey 项时跳过 erase/commit，不触碰
 * persist）；全表镜像回 def_val 并执行 ptr 直写/apply（REBOOT 项只
 * 回镜像，不 apply）。
 *
 * @return BM_OK 成功；BM_ERR_BUSY guard 拒绝；BM_ERR_NOT_INIT 未登记
 */
int bm_param_reset(void);

/**
 * @brief 设置/清除 reset 守卫回调
 *
 * @param guard 守卫回调；NULL 表示不拦截
 */
void bm_param_set_reset_guard(bm_param_reset_guard_fn_t guard);

/**
 * @brief 设置/清除 save 守卫回调
 *
 * @param guard 守卫回调；NULL 表示不拦截
 */
void bm_param_set_save_guard(bm_param_save_guard_fn_t guard);

/**
 * @brief 获取当前登记的参数条数
 *
 * @return 表项数；未登记返回 0
 */
uint16_t bm_param_count(void);

/**
 * @brief 按索引获取参数描述符
 *
 * @param idx 索引（0..bm_param_count()-1）
 * @return 描述符指针；越界或未登记返回 NULL
 */
const bm_param_desc_t *bm_param_desc_at(uint16_t idx);

/**
 * @brief 按索引获取参数当前 RAM 镜像值
 *
 * @param idx 索引（0..bm_param_count()-1）
 * @param out 输出值（非 NULL）
 * @return BM_OK 成功；BM_ERR_INVALID 索引越界或 out 为 NULL；BM_ERR_NOT_INIT 未登记
 */
int bm_param_value_at(uint16_t idx, float *out);

#ifdef __cplusplus
}
#endif

#endif /* BM_PARAM_H */
