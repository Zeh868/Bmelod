/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_schedule_map_main.c
 * @brief 通用宿主出表程序：遍历注册单元（见 bm_schedule_map_reg.h）列出的表，
 * 把每张表的 bm_tt_schedule_report()/bm_tt_schedule_report_json() 导出到
 * <outdir>/<name>.{txt,json}
 *
 * @details 本程序有意做成与注册单元无关：它自己从不点名具体的
 * bm_tt_schedule_t，只用 bm_schedule_map_reg.h 声明的 extern 全局量。
 * 这样同一份 main.c 既能链接手写的注册单元（IDE 二/三档，如
 * tests/tools/schedule_map_fixture_reg.c），也能链接 CMake 生成的
 * （bm_add_schedule_map()，Task 5），无需任何改动。
 *
 * 只要有一张表 bm_tt_schedule_init() 失败，那就是构建门禁本身：本程序
 * 把出错的表名 + 错误码报到 stderr 并返回 1，调用方（如构建步骤）视为
 * 硬失败。
 *
 * 用法: bm_schedule_map_dump <输出目录>
 * 输出目录须已存在；本程序从不创建目录。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-03
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-07-03       1.0            zeh            Task 3：通用出表 main 初版
 *
 */
#include "bm_schedule_map_reg.h"

#include <stdio.h>

/** @brief "<dir>/<name><ext>" 的长度上界（目录 + '/' + name + ".json" + NUL） */
#define SM_PATH_MAX 512

/** @brief emit 回调：把一行写入 FILE*（u 即该 FILE*） */
static void sm_emit(const char *line, void *u) {
    (void)fprintf((FILE *)u, "%s\n", line);
}

/**
 * @brief 打开 <dir>/<name><ext> 以供写入
 *
 * @param dir  输出目录（须已存在）
 * @param name 表名
 * @param ext  文件扩展名（含点号）
 * @return 成功返回 FILE*；失败返回 NULL（已报到 stderr）
 */
static FILE *sm_open(const char *dir, const char *name, const char *ext) {
    char path[SM_PATH_MAX];
    FILE *fp;

    (void)snprintf(path, sizeof path, "%s/%s%s", dir, name, ext);
    fp = fopen(path, "w");
    if (fp == NULL) {
        (void)fprintf(stderr, "schedule-map: 无法写 %s（输出目录存在吗？）\n", path);
    }
    return fp;
}

int main(int argc, char **argv) {
    if (argc < 2) {
        (void)fprintf(stderr, "用法: %s <输出目录>\n", argv[0]);
        return 1;
    }
    if (bm_schedule_map_setup() != 0) {
        (void)fprintf(stderr, "schedule-map: setup 失败（bus open？）\n");
        return 1;
    }
    for (uint32_t i = 0u; i < g_bm_schedule_map_entry_count; ++i) {
        bm_tt_schedule_t *s = g_bm_schedule_map_entries[i].sched;
        bm_tt_schedule_json_meta_t meta;
        FILE *fp;
        int rc = bm_tt_schedule_init(s);

        if (rc != BM_OK) {
            (void)fprintf(stderr,
                "schedule-map: %s init 失败 rc=%d —— 调度表排不下或参数非法，构建终止\n",
                s->name, rc);
            return 1;
        }
        fp = sm_open(argv[1], s->name, ".txt");
        if (fp == NULL) { return 1; }
        bm_tt_schedule_report(s, sm_emit, fp);
        (void)fclose(fp);

        meta.cpu = g_bm_schedule_map_entries[i].cpu;
        meta.ref_clk_hz = g_bm_schedule_map_ref_clk_hz;
        meta.operating_points_hz = g_bm_schedule_map_op_points_hz;
        meta.operating_point_count = (uint8_t)g_bm_schedule_map_op_point_count;
        fp = sm_open(argv[1], s->name, ".json");
        if (fp == NULL) { return 1; }
        bm_tt_schedule_report_json(s, &meta, sm_emit, fp);
        (void)fclose(fp);

        (void)printf("schedule-map: %s -> %s/%s.{txt,json}\n", s->name, argv[1], s->name);
    }
    return 0;
}
