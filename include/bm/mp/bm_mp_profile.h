/**
 * @file bm_mp_profile.h
 * SPDX-License-Identifier: LicenseRef-Bmeflod-Proprietary
 * @brief MP 闭源扩展公共 API · 需 bm_mp
 *
 * 未闭合 §11.1 时 `bm_mp_profile_build()` 必须失败；experimental 剖面须显式配置。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            log ring 门槛；profile epoch
 *
 */
#ifndef BM_MP_PROFILE_H
#define BM_MP_PROFILE_H

#include "bm/mp/bm_mp_types.h"
#include "bm/common/bm_types.h"

#ifndef BM_CONFIG_EXPERIMENTAL_STREAM
/** 1 = 允许未闭合 stream gate 的 experimental 剖面（禁止宣称 hard RT） */
#define BM_CONFIG_EXPERIMENTAL_STREAM  0
#endif

#ifndef BM_CONFIG_PROFILE_STREAM_GATE_ENFORCED
/** hard RT 下强制 stream gate 校验 */
#define BM_CONFIG_PROFILE_STREAM_GATE_ENFORCED  \
    (BM_CONFIG_HARD_RT_PROFILE)
#endif

#ifndef BM_CONFIG_MP_EXPERIMENTAL_STREAM
#define BM_CONFIG_MP_EXPERIMENTAL_STREAM  BM_CONFIG_EXPERIMENTAL_STREAM
#endif

#if defined(BM_CONFIG_MP_EXPERIMENTAL_STREAM) && \
    !defined(BM_CONFIG_EXPERIMENTAL_STREAM)
#define BM_CONFIG_EXPERIMENTAL_STREAM  BM_CONFIG_MP_EXPERIMENTAL_STREAM
#endif

#ifndef BM_CONFIG_MP_PROFILE_STREAM_GATE_ENFORCED
#define BM_CONFIG_MP_PROFILE_STREAM_GATE_ENFORCED  \
    BM_CONFIG_PROFILE_STREAM_GATE_ENFORCED
#endif

#if defined(BM_CONFIG_MP_PROFILE_STREAM_GATE_ENFORCED) && \
    !defined(BM_CONFIG_PROFILE_STREAM_GATE_ENFORCED)
#define BM_CONFIG_PROFILE_STREAM_GATE_ENFORCED  \
    BM_CONFIG_MP_PROFILE_STREAM_GATE_ENFORCED
#endif

#ifndef BM_CONFIG_MP_IPC_MEMORY_COHERENT_OR_NONCACHEABLE
#define BM_CONFIG_MP_IPC_MEMORY_COHERENT_OR_NONCACHEABLE 1u
#endif

#if defined(BM_CONFIG_MP_IPC_MEMORY_COHERENT_OR_NONCACHEABLE) && \
    !defined(BM_CONFIG_IPC_MEMORY_COHERENT_OR_NONCACHEABLE)
#define BM_CONFIG_IPC_MEMORY_COHERENT_OR_NONCACHEABLE  \
    BM_CONFIG_MP_IPC_MEMORY_COHERENT_OR_NONCACHEABLE
#endif

#ifndef BM_CONFIG_MP_HARD_RT_PROFILE
#define BM_CONFIG_MP_HARD_RT_PROFILE  BM_CONFIG_HARD_RT_PROFILE
#endif

#if defined(BM_CONFIG_MP_HARD_RT_PROFILE) && \
    !defined(BM_CONFIG_HARD_RT_PROFILE)
#define BM_CONFIG_HARD_RT_PROFILE  BM_CONFIG_MP_HARD_RT_PROFILE
#endif

#if BM_CONFIG_HARD_RT_PROFILE && BM_CONFIG_ENABLE_STREAM && \
    !BM_CONFIG_EXPERIMENTAL_STREAM && \
    !BM_CONFIG_PROFILE_STREAM_GATE_ENFORCED
#error "hard RT stream profile requires BM_CONFIG_PROFILE_STREAM_GATE_ENFORCED"
#endif

#if BM_CONFIG_HARD_RT_PROFILE && BM_CPU_LOCAL_ENABLE_ROUTE && \
    BM_CONFIG_ENABLE_LOG && !BM_CONFIG_LOG_RING
#error "hard RT routed profile requires BM_CONFIG_LOG_RING"
#endif

/**
 * @brief 构建并校验当前 profile（分区 + schedule + gate 闭包）
 *
 * Bootstrap 在 `bm_mp_boot_bootstrap_sequence` 之后、attach 之前调用。
 *
 * @return BM_OK 可进入 hard/block RT；负值为 fail-closed
 */
int bm_mp_profile_build(void);

/**
 * @brief 查询 profile 是否已通过 build
 */
int bm_mp_profile_is_built(void);

/** Reset boot-local profile state before rebuilding a new runtime epoch. */
void bm_mp_profile_reset(void);

#if BM_CONFIG_ENABLE_EXEC
#include "bm/hybrid/bm_exec.h"

/**
 * @brief 注册 exec 全表供 profile build 闭包校验（hard RT 必填）
 */
void bm_mp_profile_register_exec(const bm_exec_t *const *instances,
                                 uint32_t count);
#endif

/**
 * @brief 当前 profile 代际（build 成功后 >= 1）
 */
uint32_t bm_mp_profile_epoch(void);

/**
 * @brief 在当前 CPU 注册 profile 代际查询（SMP 从核须在 profile build 后调用）
 */
void bm_mp_profile_bind_epoch_on_this_cpu(void);

#endif /* BM_MP_PROFILE_H */
