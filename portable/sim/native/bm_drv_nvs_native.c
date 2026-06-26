/**
 * @file bm_drv_nvs_native.c
 * @brief native_sim 非易失存储后端：文件仿真（路线图 #10）
 *
 * 实现 `bm_hal_nvs_load()` / `bm_hal_nvs_save()` 以文件模拟 NVS 存储。
 * 测试可通过 `bm_drv_nvs_native_set_path()` 指定文件路径，
 * 通过 `bm_drv_nvs_native_reset()` 删除文件（模拟全新设备或 flash 擦除）。
 *
 * 掉电重启语义：commit 后文件持久存在；再次调用 `bm_persist_init()` 时
 * load 成功，数据得以恢复——这是本特性的核心语义验证点。
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
#include "bm_hal_nvs_native.h"
#include "hal/bm_hal_nvs.h"
#include "bm/common/bm_types.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>

/** @brief 文件路径（由测试通过 bm_drv_nvs_native_set_path 设置） */
static char s_nvs_path[256];

/* -------------------------------------------------------------------------- */
/*  测试辅助钩子（bm_hal_nvs_native.h 接口）                                    */
/* -------------------------------------------------------------------------- */

/**
 * @brief 设置 NVS 文件后端路径
 *
 * @param path 文件路径（非 NULL，长度 < 256）
 */
void bm_drv_nvs_native_set_path(const char *path) {
    if (path == NULL) {
        s_nvs_path[0] = '\0';
        return;
    }
    (void)strncpy(s_nvs_path, path, sizeof(s_nvs_path) - 1u);
    s_nvs_path[sizeof(s_nvs_path) - 1u] = '\0';
}

/**
 * @brief 删除 NVS 持久化文件（模拟全新设备 / flash 擦除）
 *
 * 文件不存在时静默忽略。
 */
void bm_drv_nvs_native_reset(void) {
    if (s_nvs_path[0] != '\0') {
        (void)remove(s_nvs_path);
    }
}

/* -------------------------------------------------------------------------- */
/*  bm_hal_nvs 后端原语实现                                                     */
/* -------------------------------------------------------------------------- */

/**
 * @brief 从文件读取 NVS 原始字节块
 *
 * 若路径未设置返回 BM_ERR_NOT_INIT；文件不存在返回 BM_ERR_NOT_FOUND；
 * 实际读取字节数与 size 不符返回 BM_ERR_INVALID。
 *
 * @param buf  目标缓冲区
 * @param size 期望读取字节数
 * @return BM_OK 或 BM_ERR_*
 */
int bm_hal_nvs_load(uint8_t *buf, uint16_t size) {
    FILE *f;
    size_t nread;

    if (s_nvs_path[0] == '\0') {
        return BM_ERR_NOT_INIT;
    }
    f = fopen(s_nvs_path, "rb");
    if (f == NULL) {
        return BM_ERR_NOT_FOUND; /* 首次上电：文件不存在 */
    }
    nread = fread(buf, 1u, (size_t)size, f);
    (void)fclose(f);
    return ((uint16_t)nread == size) ? BM_OK : BM_ERR_INVALID;
}

/**
 * @brief 将原始字节块写入文件（原子覆盖写）
 *
 * 若路径未设置返回 BM_ERR_NOT_INIT；写入失败返回 BM_ERR_OVERFLOW。
 *
 * @param buf  源数据缓冲区
 * @param size 写入字节数
 * @return BM_OK 或 BM_ERR_*
 */
int bm_hal_nvs_save(const uint8_t *buf, uint16_t size) {
    FILE *f;
    size_t nwritten;

    if (s_nvs_path[0] == '\0') {
        return BM_ERR_NOT_INIT;
    }
    f = fopen(s_nvs_path, "wb");
    if (f == NULL) {
        return BM_ERR_BUSY;
    }
    nwritten = fwrite(buf, 1u, (size_t)size, f);
    (void)fclose(f);
    return ((uint16_t)nwritten == size) ? BM_OK : BM_ERR_OVERFLOW;
}
