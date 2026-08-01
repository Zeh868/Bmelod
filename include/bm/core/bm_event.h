/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_event.h
 * @brief 发布-订阅事件总线
 *
 * 支持优先级队列、ISR 安全发布及批量消费处理。
 * 订阅表在初始化阶段构建，流式运行时冻结以确保确定性分发。
 *
 * @core_affinity 本核（per-CPU）
 * 事件总线实例仅操作调用者所在 CPU。
 * 事件路由由注册的转发钩子负责，对发布者透明。
 * @maturity E1
 * @author zeh (china_qzh@163.com)
 * @version 1.2
 * @date 2026-08-01
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-06-14       1.1            zeh            订阅冻结机制，移除分发快照，确定性流式
 * 2026-08-01       1.2            zeh            补全完整事件发布与掩码模式 HRT
 *                                                fail-closed 公开契约
 *
 */
#ifndef BM_EVENT_H
#define BM_EVENT_H

#include "bm/common/bm_types.h"

#include <stddef.h>

typedef uint8_t bm_event_type_t;              /**< 事件类型 ID */
typedef uint8_t bm_event_priority_t;          /**< 事件优先级 */
typedef uint32_t bm_event_subscriber_id_t;    /**< 订阅者句柄 */

/** 事件载荷 */
typedef struct {
    bm_event_type_t      type;
    bm_event_priority_t  priority;
    uint8_t              data_len;
    uint8_t              source_id;
    const void          *data;
} bm_event_t;

/** 订阅回调函数 */
typedef void (*bm_event_callback_t)(const bm_event_t *event, void *user_data);

typedef uint8_t (*bm_event_owner_resolver_t)(bm_event_type_t type);
typedef int (*bm_event_forwarder_t)(uint8_t target_cpu,
                                    const bm_event_t *event,
                                    const void *data,
                                    size_t len);

/**
 * @brief 设置事件路由钩子（owner 解析器 + 跨核转发器）
 *
 * @note 单核契约：须在次核 boot 前的单核 init 期调用；首次设置后闩锁
 * 禁止改写，不支持运行期动态切换路由钩子（实现见 bm_event.c）。
 *
 * @param owner_resolver 事件类型→归属 CPU 解析器
 * @param forwarder 跨核转发函数
 */
void bm_event_set_route_hooks(bm_event_owner_resolver_t owner_resolver,
                              bm_event_forwarder_t forwarder);

/**
 * @brief 重置事件子系统为初始状态（同时解冻订阅表）
 */
void bm_event_reset(void);

/**
 * @brief 冻结订阅表，进入确定性流式模式
 *
 * 调用后 bm_event_subscribe/bm_event_unsubscribe 返回 BM_ERR_BUSY。
 * 分发路径不再构建快照，直接遍历不可变链表，使 WCET 可预测。
 * bm_event_reset() 可解冻。
 */
void bm_event_freeze_subscriptions(void);

/**
 * @brief 注册事件类型
 *
 * @param type 事件类型 ID
 * @param name 类型名称（非 NULL）
 * @return BM_OK 成功；BM_ERR_ALREADY 已注册；BM_ERR_INVALID 参数无效
 */
int bm_event_register_type(bm_event_type_t type, const char *name);

/**
 * @brief 订阅指定类型的事件
 *
 * 仅允许在订阅表冻结前调用。冻结后返回 BM_ERR_BUSY。
 * 亦不可在 bm_event_process 分发回调内调用。
 *
 * @param type 事件类型 ID
 * @param cb 回调函数
 * @param user_data 用户上下文指针
 * @param id 输出订阅者句柄（可为 NULL）
 * @return BM_OK 成功；BM_ERR_NOT_INIT 类型未注册；BM_ERR_BUSY 已冻结或分发中；
 *         BM_ERR_NO_MEM 订阅表已满；BM_ERR_INVALID 参数无效
 */
int bm_event_subscribe(bm_event_type_t type, bm_event_callback_t cb,
                       void *user_data, bm_event_subscriber_id_t *id);

/**
 * @brief 取消订阅
 *
 * 仅允许在订阅表冻结前调用。冻结后返回 BM_ERR_BUSY。
 *
 * @param type 事件类型 ID
 * @param id 订阅者句柄
 * @return BM_OK 成功；BM_ERR_NOT_FOUND 未找到订阅
 */
int bm_event_unsubscribe(bm_event_type_t type, bm_event_subscriber_id_t id);

/**
 * @brief 发布事件（内部拷贝数据）
 *
 * 可在回调或主上下文调用；冻结后的确定性路径无重入限制。
 *
 * @param type 事件类型 ID
 * @param prio 事件优先级
 * @param data 载荷数据指针
 * @param len 载荷字节长度
 * @return BM_OK 成功；BM_ERR_NOT_INIT 类型未注册；
 *         BM_ERR_OVERFLOW 队列已满；BM_ERR_INVALID 参数无效
 */
int bm_event_publish_copy(bm_event_type_t type, bm_event_priority_t prio,
                          const void *data, size_t len);

/**
 * @brief 保留上游 CPU 来源并以内联拷贝方式发布事件
 *
 * 仅供事件归属核上的确定性 IPC 转发注入路径调用。
 *
 * @param type 事件类型 ID
 * @param prio 事件优先级
 * @param source_id 上游来源 CPU ID
 * @param data 载荷数据指针
 * @param len 载荷字节长度
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_NO_MEM 载荷过大；
 *         BM_ERR_NOT_INIT 类型未注册；BM_ERR_OVERFLOW 队列已满
 */
int bm_event_publish_copy_from_source(bm_event_type_t type,
                                      bm_event_priority_t prio,
                                      uint8_t source_id,
                                      const void *data,
                                      size_t len);

/**
 * @brief SRT 域 ISR 上下文发布事件（内部拷贝数据）
 *
 * 非掩码模式使用全关中断临界区，允许 HRT ISR 仅执行有界入队，不在本路径执行
 * 订阅回调；掩码模式 HRT 级 ISR 与 SRT 队列临界区不互斥，运行期返回
 * BM_ERR_BUSY。
 *
 * @param type 事件类型 ID
 * @param prio 事件优先级
 * @param data 载荷数据指针
 * @param len 载荷字节长度
 * @return BM_OK 成功；BM_ERR_OVERFLOW 队列已满；BM_ERR_NO_MEM 载荷过大；
 *         BM_ERR_NOT_INIT 类型未注册；BM_ERR_INVALID 参数无效；
 *         BM_ERR_BUSY 掩码模式下从 HRT 级 ISR 调用
 */
int bm_event_publish_copy_from_isr(bm_event_type_t type, bm_event_priority_t prio,
                                   const void *data, size_t len);

/**
 * @brief 发布完整事件结构（载荷拷贝到内联缓冲，≤ BM_CONFIG_EVENT_INLINE_DATA_SIZE）
 *
 * 默认关闭该入口并返回 BM_ERR_NOT_SUPPORTED；启用后仍复制载荷。掩码模式 HRT
 * 级上下文在参数和特性检查之前 fail-closed 返回 BM_ERR_BUSY。
 *
 * @param event 事件描述指针
 * @return BM_OK 成功；BM_ERR_OVERFLOW 队列已满；BM_ERR_NO_MEM 载荷过大；
 *         BM_ERR_NOT_INIT 类型未注册；BM_ERR_INVALID 参数无效；
 *         BM_ERR_NOT_SUPPORTED 功能关闭或 hard-RT 剖面拒绝；
 *         BM_ERR_BUSY 掩码模式 HRT 级上下文禁调
 */
int bm_event_publish_event(const bm_event_t *event);

/**
 * @brief SRT 域 ISR 上下文发布完整事件结构
 *
 * 契约上仅限 SRT 域 ISR。掩码模式 HRT 级上下文在参数和特性检查之前
 * fail-closed 返回 BM_ERR_BUSY；非掩码模式保持既有实现行为，但 HRT ISR 应使用
 * bm_event_publish_copy_from_isr()。
 *
 * @param event 事件描述指针
 * @return BM_OK 成功；BM_ERR_OVERFLOW 队列已满；BM_ERR_NO_MEM 载荷过大；
 *         BM_ERR_NOT_INIT 类型未注册；BM_ERR_INVALID 参数无效；
 *         BM_ERR_NOT_SUPPORTED 功能关闭或 hard-RT 剖面拒绝；
 *         BM_ERR_BUSY 掩码模式 HRT 级上下文禁调
 */
int bm_event_publish_event_from_isr(const bm_event_t *event);

/**
 * @brief 从队列取出并分发事件
 *
 * 非可重入：回调内再次调用返回 BM_ERR_BUSY。
 * 冻结后直接遍历不可变订阅链表分发，无快照开销。
 * ISR 必须使用 from_isr 发布 API。
 *
 * @param max_events 本次最多处理的事件条数
 * @return 实际处理的事件条数；回调重入时返回 BM_ERR_BUSY
 */
int bm_event_process(uint32_t max_events);

/**
 * @brief 查询因事件队列满而丢弃的发布次数
 *
 * @return 丢弃计数（reset 后清零）
 */
uint32_t bm_event_get_dropped_count(void);

/**
 * @brief 查询分发阶段因无效类型或队列损坏而跳过的次数
 *
 * @return 跳过计数（reset 后清零）
 */
uint32_t bm_event_get_dispatch_skipped_count(void);

/**
 * @brief 查询回调分发期间或冻结后被拒绝的 API 调用次数
 *
 * @return 最近一次成功 reset 后的拒绝次数
 */
uint32_t bm_event_get_reentrancy_rejected_count(void);

#ifdef BM_ENABLE_EVENT_TEST_HOOK
/**
 * @brief 绕过发布校验向队列注入测试事件
 * @param event 事件描述指针
 * @param prio 注入队列优先级
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；其他为队列错误码
 * @note 仅供单元测试，生产固件不得定义 BM_ENABLE_EVENT_TEST_HOOK。
 */
int bm_event_test_inject(const bm_event_t *event, bm_event_priority_t prio);
#endif

#endif /* BM_EVENT_H */
