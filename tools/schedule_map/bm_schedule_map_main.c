/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_schedule_map_main.c
 * @brief Generic host dump program: iterates the tables listed by a
 * register unit (see bm_schedule_map_reg.h) and exports each table's
 * bm_tt_schedule_report()/bm_tt_schedule_report_json() to <outdir>/<name>.{txt,json}
 *
 * @details This program is intentionally register-unit-agnostic: it never
 * names a concrete bm_tt_schedule_t itself, only the extern globals declared
 * by bm_schedule_map_reg.h. That lets the SAME main.c be linked against a
 * hand-written register unit (IDE tier-2/3, e.g.
 * tests/tools/schedule_map_fixture_reg.c) or a CMake-generated one
 * (bm_add_schedule_map(), Task 5) without any change.
 *
 * If bm_tt_schedule_init() fails for any table, that IS the build gate:
 * this program reports the offending table name + error code to stderr and
 * returns 1, which callers (e.g. a build step) treat as a hard failure.
 *
 * Usage: bm_schedule_map_dump <output-dir>
 * The output directory must already exist; this program never creates it.
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-07-03
 *
 * @par Change log:
 *
 *    Date         Version        Author          Description
 * 2026-07-03       1.0            zeh            Task 3: generic dump main
 *
 */
#include "bm_schedule_map_reg.h"

#include <stdio.h>

/** @brief Upper bound for "<dir>/<name><ext>" (directory + '/' + name + ".json" + NUL) */
#define SM_PATH_MAX 512

/** @brief emit callback: write one line to a FILE* (u is that FILE*) */
static void sm_emit(const char *line, void *u) {
    (void)fprintf((FILE *)u, "%s\n", line);
}

/**
 * @brief Open <dir>/<name><ext> for writing
 *
 * @param dir  output directory (must already exist)
 * @param name table name
 * @param ext  file extension (including the dot)
 * @return FILE* on success, NULL on failure (stderr already reported)
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
