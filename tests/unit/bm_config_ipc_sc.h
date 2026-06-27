/**
 * @file bm_config_ipc_sc.h
 * @brief 单核 IPC 测试专用配置（CPU_COUNT=1）
 *
 * 由 CMake -include 强制预包含，仅覆盖单核 IPC 测试所需的少量项，
 * 其余默认由框架 bm_config.h 补齐。勿定义 BM_CONFIG_H。
 *
 * 用途：在 CPU_COUNT==1 下独立编译 Source/core/bm_ipc.c，覆盖多核构建中
 * 被编译掉的单核分支（可靠命令环 publish/drain、默认来源读取）。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-19
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-19       1.0            zeh            正式发布
 *
 */
#ifndef BM_CONFIG_IPC_SC_H
#define BM_CONFIG_IPC_SC_H

#define BM_CONFIG_CPU_COUNT             1u
#define BM_CONFIG_ENABLE_IPC            1
#define BM_CONFIG_IPC_REL_CMD_CAPACITY  4u
#define BM_CONFIG_CACHE_LINE            64u
#define BM_CONFIG_IPC_CACHE_LINE        64u

#endif /* BM_CONFIG_IPC_SC_H */
