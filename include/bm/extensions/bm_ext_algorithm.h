/**
 * @file bm_ext_algorithm.h
 * @brief K3 外部算法静态绑定描述符与注册表接口
 *
 * 非动态插件：应用在链接期提供算法实现，init 阶段完成注册与 workspace 校验。
 *
 * @maturity K3
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-13
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-13       1.0            zeh            初始描述符与注册表桩
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#ifndef BM_EXT_ALGORITHM_H
#define BM_EXT_ALGORITHM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef BM_EXT_ALGORITHM_MAX
#define BM_EXT_ALGORITHM_MAX  8u
#endif

/**
 * 外部算法绑定描述符（链接期静态定义）
 *
 * init/step/fini 在受控阶段调用；step 不得隐式分配堆内存。
 */
typedef struct {
    const char *name;
    uint16_t    version_major;
    uint16_t    version_minor;
    uint32_t    workspace_bytes;
    int (*init)(void *workspace, uint32_t workspace_bytes, const void *config);
    int (*step)(void *workspace,
                const void *input,
                void *output,
                uint32_t len);
    void (*fini)(void *workspace);
} bm_ext_algorithm_descriptor_t;

/**
 * @brief 注册外部算法描述符
 * @return BM_OK；BM_ERR_ALREADY 同名已注册；BM_ERR_NO_MEM 槽位已满；BM_ERR_INVALID
 */
int bm_ext_algorithm_register(const bm_ext_algorithm_descriptor_t *desc);

/** @brief 按名称查找已注册描述符；未找到返回 NULL */
const bm_ext_algorithm_descriptor_t *bm_ext_algorithm_find(const char *name);

/** @brief 已注册算法数量 */
uint32_t bm_ext_algorithm_count(void);

/** @brief 按索引获取描述符（0 .. count-1）；越界返回 NULL */
const bm_ext_algorithm_descriptor_t *bm_ext_algorithm_get(uint32_t index);

/** @brief 清空注册表（测试或重配置用） */
void bm_ext_algorithm_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* BM_EXT_ALGORITHM_H */
