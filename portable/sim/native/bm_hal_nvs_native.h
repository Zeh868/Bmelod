/**
 * @file bm_hal_nvs_native.h
 * @brief native_sim NVS 测试辅助接口（路线图 #10）
 *
 * 提供测试专用的 NVS 文件路径配置与重置函数，供单元测试模拟
 * 掉电重启语义：
 * 1. `bm_drv_nvs_native_set_path()` 指定持久化文件路径；
 * 2. `bm_drv_nvs_native_reset()` 删除该文件（模拟全新设备）；
 * 3. 调用 `bm_persist_init()` 加载（或首次上电空表）；
 * 4. 调用 `bm_persist_set()` + `bm_persist_commit()` 写入并落盘；
 * 5. 再次调用 `bm_persist_init()` 从文件恢复——此即掉电重启后仍在的语义验证。
 *
 * 典型用法：
 * @code
 * // setUp:
 * bm_drv_nvs_native_set_path("_bm_persist_test.bin");
 * bm_drv_nvs_native_reset();   // 删除旧文件
 *
 * bm_persist_init();           // 空表启动
 * bm_persist_set("k", &v, sizeof(v));
 * bm_persist_commit();         // 写入文件
 *
 * // 模拟掉电重启：
 * bm_persist_init();           // 从文件重新加载
 * bm_persist_get("k", &v2, sizeof(v2), &len); // 应与 v 一致
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
#ifndef BM_HAL_NVS_NATIVE_H
#define BM_HAL_NVS_NATIVE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 设置 NVS 文件后端路径
 *
 * 必须在 bm_persist_init() 之前调用，否则 load/save 返回 BM_ERR_NOT_INIT。
 * 路径由调用方保证生命周期覆盖整个测试期间。
 *
 * @param path 文件路径字符串（非 NULL，长度 < 256）
 */
void bm_drv_nvs_native_set_path(const char *path);

/**
 * @brief 删除 NVS 持久化文件（模拟全新设备/擦除 flash）
 *
 * 若文件不存在，静默忽略。
 * 在 setUp 中调用以确保每个测试从干净状态开始。
 */
void bm_drv_nvs_native_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* BM_HAL_NVS_NATIVE_H */
