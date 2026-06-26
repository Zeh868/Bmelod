/**
 * @file bm_persist.h
 * @brief 键值（KV）持久化存储公共 API（路线图 #10）
 *
 * 提供运行期可调参数的掉电保存能力。底层由 `bm_hal_nvs_load()`/
 * `bm_hal_nvs_save()` 后端支撑；native_sim 使用文件仿真非易失存储，
 * 嵌入式 flash/EEPROM 后端留桩待上板。
 *
 * 使用语义：
 * - `bm_persist_init()`：从 NVS 后端加载持久化数据到 RAM 表；
 *   首次上电（无数据）时 RAM 表为空，返回 BM_OK；
 * - `bm_persist_set()` / `bm_persist_erase()`：仅更新 RAM 表；
 * - `bm_persist_commit()`：将 RAM 表序列化并写入 NVS 后端（落盘）；
 * - `bm_persist_get()`：从 RAM 表读取。
 *
 * 典型用法：
 * @code
 * // 启动：加载上次保存的参数
 * bm_persist_init();
 *
 * float kp = 1.0f;
 * uint16_t len;
 * if (bm_persist_get("pid_kp", &kp, sizeof(kp), &len) == BM_OK) {
 *     // 使用持久化参数
 * }
 *
 * // 运行中调整并保存
 * float new_kp = 1.5f;
 * bm_persist_set("pid_kp", &new_kp, sizeof(new_kp));
 * bm_persist_commit();
 * @endcode
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-26
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-26       1.0            zeh            正式发布（路线图 #10 参数/配置持久化）
 *
 */
#ifndef BM_PERSIST_H
#define BM_PERSIST_H

#include "bm/common/bm_types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化持久化存储，从 NVS 后端加载现有数据
 *
 * 可重复调用以模拟掉电重启（重新从后端加载）。
 * 若后端无数据（首次上电）或后端不可用，RAM 表清空，仍返回 BM_OK。
 *
 * @return BM_OK 成功（含首次上电空表情况）
 */
int bm_persist_init(void);

/**
 * @brief 读取键对应的值
 *
 * 从 RAM 表中查找 key，将值拷贝至 buf。
 *
 * @param key     键名（非 NULL，非空字符串，最多 BM_CONFIG_PERSIST_KEY_MAX_LEN 字节）
 * @param buf     目标缓冲区（非 NULL）
 * @param cap     缓冲区容量（字节）
 * @param out_len 实际读取字节数（非 NULL，输出参数）
 * @return BM_OK 成功；BM_ERR_NOT_FOUND 键不存在；BM_ERR_OVERFLOW 缓冲区不足；
 *         BM_ERR_INVALID 参数无效；BM_ERR_NOT_INIT 未初始化
 */
int bm_persist_get(const char *key, void *buf, uint16_t cap, uint16_t *out_len);

/**
 * @brief 写入键值（仅更新 RAM 表，需调用 commit 才落盘）
 *
 * 若 key 已存在则覆盖写；否则新建条目。
 *
 * @param key  键名（非 NULL，非空，最多 BM_CONFIG_PERSIST_KEY_MAX_LEN 字节）
 * @param data 值数据（非 NULL）
 * @param len  值字节数（<= BM_CONFIG_PERSIST_VAL_MAX_LEN）
 * @return BM_OK 成功；BM_ERR_NO_MEM 表已满（无空闲条目）；
 *         BM_ERR_OVERFLOW 值超长；BM_ERR_INVALID 参数无效；
 *         BM_ERR_NOT_INIT 未初始化
 */
int bm_persist_set(const char *key, const void *data, uint16_t len);

/**
 * @brief 删除指定键（仅更新 RAM 表，需调用 commit 才落盘）
 *
 * @param key 键名（非 NULL）
 * @return BM_OK 成功；BM_ERR_NOT_FOUND 键不存在；BM_ERR_INVALID 参数无效；
 *         BM_ERR_NOT_INIT 未初始化
 */
int bm_persist_erase(const char *key);

/**
 * @brief 将 RAM 表中的所有 KV 条目序列化并写入 NVS 后端（落盘）
 *
 * 无后端（无 BM_DRV_HAS_BACKEND）时为 no-op，返回 BM_OK。
 *
 * @return BM_OK 成功；BM_ERR_NOT_INIT 未初始化；其他 BM_ERR_* 写入失败
 */
int bm_persist_commit(void);

#ifdef __cplusplus
}
#endif

#endif /* BM_PERSIST_H */
