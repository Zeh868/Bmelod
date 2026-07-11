/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_shell.h
 * @brief 轻量级非阻塞串口命令行
 *
 * 支持字符流喂入、命令注册与 Console CLI 轮询，适用于裸机调试。
 *
 * @par 交互能力（v1.1）
 *   - **Tab 补全**：行首命令词前缀匹配——唯一匹配补全余下字符并回显；
 *     多个匹配换行列出候选后重绘提示符与已输入前缀；无匹配响铃 \\a。
 *   - **内建 help**：用户未注册同名命令时，`help` 由框架兜底实现——
 *     遍历命令表打印「名字 + 帮助文本」；用户注册的 help 命令优先。
 *   - **前台模态命令**：命令 handler 调 bm_shell_modal_enter() 进入模态，
 *     长驻前台流式输出（如 watch 遥测），Ctrl+C（0x03）退出。契约见
 *     bm_shell_modal_enter() 注释。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.1
 * @date 2026-07-11
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-07-11       1.1            zeh            shell 交互批①：前台模态命令（Ctrl+C 退出）+ Tab 补全 + 内建 help 兜底
 *
 */
#ifndef BM_SHELL_H
#define BM_SHELL_H

#include "bm/common/bm_types.h"

#include <stdint.h>

#ifndef BM_CONFIG_SHELL_BUF_SIZE
#define BM_CONFIG_SHELL_BUF_SIZE 64
#endif

#ifndef BM_CONFIG_SHELL_MAX_ARGS
#define BM_CONFIG_SHELL_MAX_ARGS 4
#endif

#ifndef BM_CONFIG_SHELL_MAX_CMDS
#define BM_CONFIG_SHELL_MAX_CMDS 8
#endif

#ifndef BM_CONFIG_SHELL_MAX_NAME_LEN
#define BM_CONFIG_SHELL_MAX_NAME_LEN 16
#endif

#if BM_CONFIG_SHELL_MAX_NAME_LEN < 2
#error "BM_CONFIG_SHELL_MAX_NAME_LEN 至少为 2"
#endif

#if BM_CONFIG_SHELL_BUF_SIZE < 2 || BM_CONFIG_SHELL_BUF_SIZE > 256
#error "BM_CONFIG_SHELL_BUF_SIZE 须在 2..256 范围内"
#endif

#if BM_CONFIG_SHELL_MAX_ARGS < 1
#error "BM_CONFIG_SHELL_MAX_ARGS 至少为 1"
#endif

#if BM_CONFIG_SHELL_MAX_CMDS < 1 || BM_CONFIG_SHELL_MAX_CMDS > 255
#error "BM_CONFIG_SHELL_MAX_CMDS 须在 1..255 范围内"
#endif

/** 命令处理函数 */
typedef int (*bm_shell_cmd_fn_t)(int argc, char *argv[]);

/**
 * @brief 模态命令 tick 回调：模态期间每次 bm_shell_poll 调用一次。
 *
 * 限频由回调自理（如用 bm_uptime_us() 自判间隔），框架不做节流。
 * 须短、非阻塞（主循环上下文）。
 *
 * @param ctx bm_shell_modal_enter() 传入的用户上下文。
 */
typedef void (*bm_shell_modal_tick_fn_t)(void *ctx);

/**
 * @brief 模态命令 stop 回调：Ctrl+C 退出模态时调用一次。
 *
 * **契约：stop 负责把业务带回安全态**——若模态命令启动了任何持续动作
 * （出力/采样/占用资源），必须在此回调内撤销；只读型命令（如 watch）
 * 无此负担，可传 NULL。
 *
 * @param ctx bm_shell_modal_enter() 传入的用户上下文。
 */
typedef void (*bm_shell_modal_stop_fn_t)(void *ctx);

/** 命令注册条目 */
typedef struct {
    const char       *name;
    bm_shell_cmd_fn_t fn;
    const char       *help;
} bm_shell_cmd_t;

/** Shell 实例状态 */
typedef struct {
    char          buf[BM_CONFIG_SHELL_BUF_SIZE];
    uint8_t       cursor;
    bm_shell_cmd_t cmds[BM_CONFIG_SHELL_MAX_CMDS];
    char           cmd_names[BM_CONFIG_SHELL_MAX_CMDS]
                             [BM_CONFIG_SHELL_MAX_NAME_LEN];
    uint8_t       cmd_count;
    uint8_t       swallow_lf;
    /** 非 0 表示处于前台模态（静态单槽，不支持嵌套） */
    uint8_t                  modal_active;
    /** 模态 tick 回调（模态期间每次 poll 调一次） */
    bm_shell_modal_tick_fn_t modal_tick;
    /** 模态 stop 回调（Ctrl+C 退出时调一次，可为 NULL） */
    bm_shell_modal_stop_fn_t modal_stop;
    /** 模态回调用户上下文 */
    void                    *modal_ctx;
} bm_shell_t;

/** 静态定义 Shell 实例 */
#define BM_SHELL_DEFINE(name) \
    static bm_shell_t name = { \
        .cursor = 0, \
        .cmd_count = 0, .swallow_lf = 0, \
        .modal_active = 0 \
    }

/**
 * @brief 初始化 Shell 实例
 *
 * @param shell Shell 实例指针
 */
void bm_shell_init(bm_shell_t *shell);

/**
 * @brief 注册 Shell 命令
 *
 * @param shell Shell 实例指针
 * @param name 命令名称（注册时复制，长度必须小于 BM_CONFIG_SHELL_MAX_NAME_LEN）
 * @param fn 命令处理函数
 * @param help 帮助文本（可为 NULL）
 * @return BM_OK 成功；BM_ERR_NO_MEM 命令表已满；BM_ERR_INVALID 参数无效
 */
int bm_shell_register(bm_shell_t *shell, const char *name,
                      bm_shell_cmd_fn_t fn, const char *help);

/**
 * @brief 喂入单个字符，完整行到达时立即执行
 *
 * 非 ISR 安全；仅从主循环或 bm_shell_poll 调用。
 *
 * @param shell Shell 实例指针
 * @param c 输入字符
 */
void bm_shell_feed(bm_shell_t *shell, char c);

/**
 * @brief 轮询 Console CLI 并处理新字符（非阻塞）
 *
 * @param shell Shell 实例指针
 */
void bm_shell_poll(bm_shell_t *shell);

/**
 * @brief 直接执行命令行（就地分词，会修改 line 内容）
 *
 * @param shell Shell 实例指针
 * @param line 命令行缓冲区（可被修改）
 * @return 命令处理函数的返回值；未找到命令时返回 BM_ERR_NOT_FOUND
 */
int bm_shell_exec(bm_shell_t *shell, char *line);

/**
 * @brief 通过 Console CLI 通道输出字符串
 *
 * @param s 以 NUL 结尾的字符串
 */
void bm_shell_puts(const char *s);

/**
 * @brief 进入前台模态：命令长驻前台流式输出，Ctrl+C（0x03）退出。
 *
 * 通常由命令 handler 调用。进入模态后 bm_shell_poll 行为改变：
 *   - 每次 poll 先扫输入：仅识别 0x03（Ctrl+C），**其余输入字符一律丢弃**
 *     （静态单槽最简设计，不缓存不回显）；
 *   - 收到 0x03 → 调 stop_fn（若非 NULL）→ 清模态态 → 打印 "^C\\r\\n$ "
 *     回到提示符；
 *   - 未退出则每次 poll 调一次 tick_fn（限频由 tick_fn 自理）。
 *
 * **stop 语义契约**：模态命令的 stop_fn 负责把业务带回安全态（撤销模态
 * 期间启动的一切持续动作）；只读命令可传 NULL。
 *
 * 零动态内存、静态单槽：**不支持嵌套模态**，模态中再调用本函数返回
 * BM_ERR_BUSY。
 *
 * @param shell   Shell 实例指针。
 * @param tick_fn 模态 tick 回调（必填）。
 * @param stop_fn 模态 stop 回调（可为 NULL，仅限只读命令）。
 * @param ctx     透传给回调的用户上下文（可为 NULL）。
 * @return BM_OK 成功进入；BM_ERR_INVALID shell/tick_fn 为空；
 *         BM_ERR_BUSY 已处于模态（不支持嵌套）。
 */
int bm_shell_modal_enter(bm_shell_t *shell, bm_shell_modal_tick_fn_t tick_fn,
                         bm_shell_modal_stop_fn_t stop_fn, void *ctx);

/**
 * @brief 查询是否处于前台模态。
 *
 * @param shell Shell 实例指针。
 * @return 非 0 处于模态；0 空闲或 shell 为空。
 */
int bm_shell_modal_active(const bm_shell_t *shell);

#endif /* BM_SHELL_H */
