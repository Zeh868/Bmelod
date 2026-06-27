/**
 * @file bm_runtime_model.h
 * @brief 框架运行时实例模型说明（每 MCU 单运行时）
 *
 * Bmelod Baremetal 在单颗 MCU 上采用「每子系统一个静态运行时」模型，以换取
 * 可预测的 RAM 占用与 WCET。多轴/多 Pack 控制通过多个 bm_exec_t 实现，
 * 而非多个事件总线或 HRT 调度器。
 *
 * 可观测性：各子系统提供 get_session / is_started 等只读查询 API，供测试、
 * 诊断与安全监督逻辑使用。详见运行时实例相关设计文档。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-06-12
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-11       1.0            zeh            正式发布
 * 2026-06-12       1.1            zeh            增加 BM_RUNTIME_STREAM
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_RUNTIME_MODEL_H
#define BM_RUNTIME_MODEL_H

/**
 * @brief 框架子系统运行时范围（编译期单实例）
 *
 * 下列组件在链接镜像中各有一份静态状态；应用不得假定可并存第二套实例。
 */
typedef enum {
    BM_RUNTIME_EVENT,       /**< bm_event：全局事件总线 */
    BM_RUNTIME_MODULE,      /**< bm_module：模块表 */
    BM_RUNTIME_WDG,         /**< bm_wdg：软件看门狗聚合 */
    BM_RUNTIME_HRT,         /**< bm_hrt：Scheduled 定时分发 */
    BM_RUNTIME_TICKER,      /**< bm_ticker：毫秒 ticker */
    BM_RUNTIME_EXEC,        /**< bm_exec：执行实例批次与会话 */
    BM_RUNTIME_STREAM,      /**< bm_stream：块流运行时（启用 STREAM 时） */
    BM_RUNTIME_SYNC         /**< bm_sync：活动同步域 */
} bm_runtime_subsystem_t;

#endif /* BM_RUNTIME_MODEL_H */
