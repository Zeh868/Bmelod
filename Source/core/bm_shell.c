/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_shell.c
 * @brief 轻量级非阻塞串口命令行实现
 *
 * 字符回显、退格处理与命令分词执行；通过 Console CLI 通道收发。
 * v1.1 新增：前台模态命令（Ctrl+C 退出）、Tab 命令补全、内建 help 兜底。
 * v1.2 新增：行历史（↑/↓ 回翻）、ESC 序列吞噬（修箭头键注入垃圾字符）。
 * v1.3 新增：参数区 Tab 补全（命令可选登记补全回调，复用命令词补全骨架）。
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.3
 * @date 2026-07-18
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-10       1.0            zeh            正式发布
 * 2026-07-11       1.1            zeh            shell 交互批①：前台模态命令（Ctrl+C 退出）+ Tab 补全 + 内建 help 兜底
 * 2026-07-11       1.2            zeh            shell 交互批②：行历史（↑/↓ 回翻）+ ESC 序列吞噬（修箭头键注入垃圾字符）
 * 2026-07-18       1.3            zeh            shell 交互批③：参数区 Tab 补全——新增
 *                                                 bm_shell_set_completer()，_tab_complete
 *                                                 拆出 _complete_unique/_complete_list_*
 *                                                 共用收尾逻辑供参数区补全复用
 *
 */
#include "bm_shell.h"
#include "bm_log.h"

#include <stddef.h>
#include <string.h>

/* shell CLI 通道端口函数（由 HAL 层实现，避免 core 直接依赖 Console HAL） */
extern int    bm_shell_port_write(const uint8_t *data, size_t len);
extern size_t bm_shell_port_read(uint8_t *data, size_t max_len);

/**
 * @brief 简易字符串比较（减少对完整 libc 的依赖）
 *
 * @param a 第一个字符串
 * @param b 第二个字符串
 * @return 相等返回 0；不等返回字符差值
 */
static int _strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/**
 * @brief 经 shell CLI 端口发送以 NUL 结尾的字符串
 *
 * @param s 字符串指针
 */
static void _puts(const char *s) {
    if (s) {
        (void)bm_shell_port_write((const uint8_t *)s, strlen(s));
    }
}

/**
 * @brief 判断 b 是否为 a 的前缀
 *
 * @param a 完整字符串
 * @param b 前缀候选
 * @return 非 0 表示 b 是 a 的前缀（含相等）；0 表示不是
 */
static int _starts_with(const char *a, const char *b) {
    while (*b) {
        if (*a != *b) return 0;
        a++; b++;
    }
    return 1;
}

/**
 * @brief 初始化 Shell 上下文与命令表
 *
 * @param shell Shell 实例指针
 */
void bm_shell_init(bm_shell_t *shell) {
    if (!shell) return;
    memset(shell->buf, 0, sizeof(shell->buf));
    shell->cursor = 0;
    shell->cmd_count = 0;
    shell->swallow_lf = 0;
    shell->modal_active = 0;
    shell->modal_tick = NULL;
    shell->modal_stop = NULL;
    shell->modal_ctx = NULL;
    shell->esc_state = 0;
#if BM_CONFIG_SHELL_HISTORY_DEPTH > 0
    shell->hist_count = 0;
    shell->hist_head = 0;
    shell->hist_nav = 0;
#endif
    BM_LOGI("shell", "init");
}

/**
 * @brief 注册一条 Shell 命令
 *
 * @param shell Shell 实例指针
 * @param name 命令名称
 * @param fn 命令处理函数
 * @param help 帮助文本（可为 NULL）
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_NO_MEM 命令表已满
 */
int bm_shell_register(bm_shell_t *shell, const char *name,
                      bm_shell_cmd_fn_t fn, const char *help) {
    size_t name_len;

    if (!shell || !name || !fn) return BM_ERR_INVALID;
    name_len = strlen(name);
    if (name_len == 0u || name_len >= BM_CONFIG_SHELL_MAX_NAME_LEN) {
        return BM_ERR_INVALID;
    }
    if (shell->cmd_count >= BM_CONFIG_SHELL_MAX_CMDS) {
        BM_LOGW("shell", "register table full");
        return BM_ERR_NO_MEM;
    }

    memcpy(shell->cmd_names[shell->cmd_count], name, name_len + 1u);
    shell->cmds[shell->cmd_count].name =
        shell->cmd_names[shell->cmd_count];
    shell->cmds[shell->cmd_count].fn = fn;
    shell->cmds[shell->cmd_count].help = help;
    /* 补全器随注册清零：挡复用同一 shell 实例（如单测反复 init/register）
     * 时遗留上一轮登记的悬挂 completer/ctx */
    shell->cmds[shell->cmd_count].completer = NULL;
    shell->cmds[shell->cmd_count].completer_ctx = NULL;
    shell->cmd_count++;
    BM_LOGD("shell", "cmd '%s' registered", name);
    return BM_OK;
}

/**
 * @brief 登记/更新命令的参数区 Tab 补全回调（详见头文件同名函数注释）
 *
 * @param shell     Shell 实例指针
 * @param name      目标命令名（须已注册）
 * @param completer 补全回调；NULL 清除
 * @param user_ctx  透传给 completer 的用户上下文
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_NOT_FOUND 命令未注册
 */
int bm_shell_set_completer(bm_shell_t *shell, const char *name,
                           bm_shell_completer_fn_t completer, void *user_ctx) {
    if (!shell || !name) return BM_ERR_INVALID;

    for (uint8_t i = 0; i < shell->cmd_count; i++) {
        if (_strcmp(shell->cmds[i].name, name) == 0) {
            shell->cmds[i].completer = completer;
            shell->cmds[i].completer_ctx = user_ctx;
            return BM_OK;
        }
    }
    return BM_ERR_NOT_FOUND;
}

/**
 * @brief 就地分词命令行，返回参数个数
 *
 * @param line 可修改的命令行缓冲区
 * @param argv 参数指针数组
 * @param max_argv 最大参数个数
 * @return 解析得到的 argc
 */
static int _tokenize(char *line, char *argv[], int max_argv) {
    int argc = 0;
    char *p = line;

    /* 就地分词：将分隔符替换为 \0，argv 指向各参数首地址 */
    while (*p) {
        while (*p == ' ' || *p == '\t') *p++ = '\0';
        if (!*p) break;
        if (argc >= max_argv) {
            return -1;
        }
        argv[argc++] = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        if (*p) {
            *p++ = '\0';
        }
    }
    return argc;
}

/**
 * @brief 解析并执行一条命令行
 *
 * @param shell Shell 实例指针
 * @param line 可修改的命令行缓冲区
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_NOT_FOUND 未知命令；其他为命令返回值
 */
int bm_shell_exec(bm_shell_t *shell, char *line) {
    if (!shell || !line) return BM_ERR_INVALID;

    char *argv[BM_CONFIG_SHELL_MAX_ARGS];
    int argc = _tokenize(line, argv, BM_CONFIG_SHELL_MAX_ARGS);
    if (argc < 0) {
        BM_LOGW("shell", "too many arguments");
        return BM_ERR_INVALID;
    }
    if (argc == 0) {
        return BM_OK;
    }
    if (shell->cmd_count > BM_CONFIG_SHELL_MAX_CMDS) {
        return BM_ERR_INVALID;
    }

    for (uint8_t i = 0; i < shell->cmd_count; i++) {
        if (_strcmp(argv[0], shell->cmds[i].name) == 0) {
            BM_LOGD("shell", "exec '%s'", argv[0]);
            return shell->cmds[i].fn(argc, argv);
        }
    }

    /* 内建 help 兜底：用户未注册同名命令时由框架实现（表命中优先，
     * 上方循环已保证）。遍历命令表打印「名字 + 帮助文本」。 */
    if (_strcmp(argv[0], "help") == 0) {
        _puts("commands:\r\n");
        for (uint8_t i = 0; i < shell->cmd_count; i++) {
            _puts("  ");
            _puts(shell->cmds[i].name);
            if (shell->cmds[i].help) {
                _puts(" - ");
                _puts(shell->cmds[i].help);
            }
            _puts("\r\n");
        }
        _puts("  help - list commands\r\n");
        return BM_OK;
    }

    BM_LOGW("shell", "unknown cmd '%s'", argv[0]);
    _puts("unknown command: ");
    _puts(argv[0]);
    _puts("\r\n");
    return BM_ERR_NOT_FOUND;
}

/**
 * @brief 退出前台模态：调 stop 回调、清模态态、重绘提示符
 *
 * 先清模态态再调 stop 回调，保证 stop 内（若间接触发输出/查询）看到的
 * shell 已回到空闲态；随后打印 "^C" + 换行 + 提示符。
 *
 * @param shell Shell 实例指针（调用方保证非 NULL 且处于模态）
 */
static void _modal_exit(bm_shell_t *shell) {
    bm_shell_modal_stop_fn_t stop = shell->modal_stop;
    void                    *ctx  = shell->modal_ctx;

    shell->modal_active = 0;
    shell->modal_tick = NULL;
    shell->modal_stop = NULL;
    shell->modal_ctx = NULL;
    shell->esc_state = 0; /* 模态期丢字符可能吃掉半截 ESC 序列，退出时归零 */
    if (stop) {
        stop(ctx);
    }
    _puts("^C\r\n$ ");
}

/**
 * @brief 唯一匹配收尾：把 match 中 prefix_len 之后的剩余字符追加进行缓冲
 *        并逐字符回显（受行缓冲上限约束）。命令词补全与参数区补全共用。
 *
 * @param shell      Shell 实例指针（调用方保证非 NULL）
 * @param match      完整候选词（NUL 结尾）
 * @param prefix_len 已输入前缀长度（match 的前 prefix_len 字符即已输入内容）
 */
static void _complete_unique(bm_shell_t *shell, const char *match, uint8_t prefix_len) {
    const char *rest = match + prefix_len;
    while (*rest && shell->cursor < BM_CONFIG_SHELL_BUF_SIZE - 1) {
        shell->buf[shell->cursor++] = *rest;
        (void)bm_shell_port_write((const uint8_t *)rest, 1u);
        rest++;
    }
}

/** @brief 多候选列表起始：换行。命令词补全与参数区补全共用。 */
static void _complete_list_begin(void) {
    _puts("\r\n");
}

/** @brief 多候选列表条目：候选词 + 两个空格分隔。 */
static void _complete_list_item(const char *cand) {
    _puts(cand);
    _puts("  ");
}

/** @brief 多候选列表收尾：换行 + 重绘提示符与已输入整行（buf 已 NUL 结尾）。 */
static void _complete_list_end(bm_shell_t *shell) {
    _puts("\r\n$ ");
    _puts(shell->buf);
}

/**
 * @brief 参数区 Tab 补全候选收集器（栈上定长，零 malloc）。
 *
 * 容量/单条候选长度上限分别复用 BM_CONFIG_SHELL_MAX_COMPLETIONS 与
 * BM_CONFIG_SHELL_MAX_NAME_LEN（沿用命令名长度上限，参数名与命令名同属
 * 点分小写短标识符量级，无需另立宏）。
 */
typedef struct {
    char (*cand)[BM_CONFIG_SHELL_MAX_NAME_LEN]; /**< 候选词存储（调用方栈上数组） */
    uint16_t count;                              /**< 已收集条数 */
    uint16_t cap;                                 /**< 容量上限 */
} _complete_collect_t;

/**
 * @brief 参数区补全 emit 回调实现：越界候选静默丢弃，超长候选截断。
 *
 * @param emit_ctx  _complete_collect_t 指针
 * @param candidate 候选词（NUL 结尾）
 */
static void _complete_emit(void *emit_ctx, const char *candidate) {
    _complete_collect_t *c = (_complete_collect_t *)emit_ctx;
    size_t len;

    if (!c || !candidate || c->count >= c->cap) {
        return;
    }
    len = strlen(candidate);
    if (len >= (size_t)BM_CONFIG_SHELL_MAX_NAME_LEN) {
        len = (size_t)BM_CONFIG_SHELL_MAX_NAME_LEN - 1u;
    }
    memcpy(c->cand[c->count], candidate, len);
    c->cand[c->count][len] = '\0';
    c->count++;
}

/**
 * @brief 参数区 Tab 补全：命令若登记了补全器，按（参数序号，当前词前缀）
 *        取候选词，复用命令词补全同一套"唯一匹配自动补全 / 多候选列出 /
 *        无匹配响铃"逻辑；未登记补全器的命令响铃退出（向后兼容）。
 *
 * @param shell    Shell 实例指针（调用方保证非 NULL；shell->buf 已在
 *                 shell->cursor 处 NUL 结尾）
 * @param first_ws 命令词后第一个空白在 buf 中的下标（< shell->cursor）
 */
static void _tab_complete_args(bm_shell_t *shell, uint8_t first_ws) {
    char    cmd_name[BM_CONFIG_SHELL_MAX_NAME_LEN];
    uint8_t cmd_idx = 0;
    uint8_t found = 0;
    uint8_t prefix_start;
    uint8_t argv_idx;
    uint8_t in_token;
    uint8_t i;
    char    cand[BM_CONFIG_SHELL_MAX_COMPLETIONS][BM_CONFIG_SHELL_MAX_NAME_LEN];
    _complete_collect_t collect;

    if (first_ws >= (uint8_t)sizeof(cmd_name)) {
        /* 命令名超长不可能命中注册表（注册时已限长），直接响铃 */
        _puts("\a");
        return;
    }
    memcpy(cmd_name, shell->buf, first_ws);
    cmd_name[first_ws] = '\0';

    for (i = 0; i < shell->cmd_count; i++) {
        if (_strcmp(shell->cmds[i].name, cmd_name) == 0) {
            cmd_idx = i;
            found = 1;
            break;
        }
    }
    if (!found || shell->cmds[cmd_idx].completer == NULL) {
        _puts("\a");
        return;
    }

    /* 定位当前词前缀起点：cursor 前最后一个空白之后 */
    prefix_start = 0;
    for (i = 0; i < shell->cursor; i++) {
        if (shell->buf[i] == ' ' || shell->buf[i] == '\t') {
            prefix_start = (uint8_t)(i + 1u);
        }
    }
    /* 统计 [0,prefix_start) 内完整 token 数 = 当前词的 argv 序号
     * （argv[0]=命令词，故命令词自身计入一次；不含当前正在输入的这个词） */
    argv_idx = 0;
    in_token = 0;
    for (i = 0; i < prefix_start; i++) {
        if (shell->buf[i] == ' ' || shell->buf[i] == '\t') {
            in_token = 0;
        } else if (!in_token) {
            in_token = 1;
            argv_idx++;
        }
    }

    collect.cand  = cand;
    collect.count = 0;
    collect.cap   = (uint16_t)BM_CONFIG_SHELL_MAX_COMPLETIONS;
    shell->cmds[cmd_idx].completer(argv_idx, &shell->buf[prefix_start],
                                   (uint8_t)(shell->cursor - prefix_start),
                                   _complete_emit, &collect,
                                   shell->cmds[cmd_idx].completer_ctx);

    if (collect.count == 0u) {
        _puts("\a");
        return;
    }
    if (collect.count == 1u) {
        _complete_unique(shell, cand[0], (uint8_t)(shell->cursor - prefix_start));
        return;
    }
    _complete_list_begin();
    for (i = 0; i < collect.count; i++) {
        _complete_list_item(cand[i]);
    }
    _complete_list_end(shell);
}

/**
 * @brief Tab 命令补全：行首补命令词，参数区（若命令登记了补全器）委托
 *        _tab_complete_args。
 *
 * 命令词候选集 = 已注册命令表 ∪ 内建 "help"（用户未注册同名命令时）：
 *   - 唯一匹配：补全余下字符并回显；
 *   - 多个匹配：换行列出全部候选，重绘提示符与已输入前缀；
 *   - 无匹配：响铃 \\a。
 * 参数区（前缀已含空白）：命令若经 bm_shell_set_completer() 登记补全器，
 * 按上述同一套骨架处理该补全器给出的候选；未登记则响铃（向后兼容）。
 * 遍历命令表 O(N)，N 有界（BM_CONFIG_SHELL_MAX_CMDS）。
 *
 * @param shell Shell 实例指针（调用方保证非 NULL）
 */
static void _tab_complete(bm_shell_t *shell) {
    const char *match = NULL;
    uint16_t    n_match = 0;
    uint8_t     help_registered = 0;
    uint8_t     ws_pos = shell->cursor; /* 哨兵：未见空白 = 仍在命令词区 */
    uint8_t     i;

    for (i = 0; i < shell->cursor; i++) {
        if (shell->buf[i] == ' ' || shell->buf[i] == '\t') {
            ws_pos = i;
            break;
        }
    }
    shell->buf[shell->cursor] = '\0';

    if (ws_pos != shell->cursor) {
        /* 已进入参数区：委托 completer（未登记则内部响铃），不再走
         * 下方命令词前缀匹配 */
        _tab_complete_args(shell, ws_pos);
        return;
    }

    for (i = 0; i < shell->cmd_count; i++) {
        if (_strcmp(shell->cmds[i].name, "help") == 0) {
            help_registered = 1;
        }
        if (_starts_with(shell->cmds[i].name, shell->buf)) {
            n_match++;
            match = shell->cmds[i].name;
        }
    }
    /* 内建 help 作为虚拟候选（与 bm_shell_exec 的 help 兜底一致） */
    if (!help_registered && _starts_with("help", shell->buf)) {
        n_match++;
        match = "help";
    }

    if (n_match == 0) {
        _puts("\a");
        return;
    }

    if (n_match == 1) {
        _complete_unique(shell, match, shell->cursor);
        return;
    }

    /* 多个匹配：换行列出候选，重绘提示符与已输入前缀 */
    _complete_list_begin();
    for (i = 0; i < shell->cmd_count; i++) {
        if (_starts_with(shell->cmds[i].name, shell->buf)) {
            _complete_list_item(shell->cmds[i].name);
        }
    }
    if (!help_registered && _starts_with("help", shell->buf)) {
        _complete_list_item("help");
    }
    _complete_list_end(shell);
}

#if BM_CONFIG_SHELL_HISTORY_DEPTH > 0
/**
 * @brief 整行替换：擦除当前行显示，src 非 NULL 时拷入并回显。
 *
 * @param shell Shell 实例（调用方保证非 NULL）
 * @param src   替换内容；NULL = 仅清空行
 */
static void _line_replace(bm_shell_t *shell, const char *src) {
    while (shell->cursor > 0) {
        _puts("\b \b");
        shell->cursor--;
    }
    shell->buf[0] = '\0';
    if (src != NULL) {
        size_t n = strlen(src);
        if (n > (size_t)(BM_CONFIG_SHELL_BUF_SIZE - 1)) {
            n = (size_t)(BM_CONFIG_SHELL_BUF_SIZE - 1);
        }
        (void)memcpy(shell->buf, src, n);
        shell->buf[n] = '\0';
        shell->cursor = (uint8_t)n;
        _puts(shell->buf);
    }
}

/**
 * @brief 取浏览位置对应的历史条目。
 *
 * @param shell Shell 实例
 * @param nav   浏览位置（1=最新..hist_count=最旧）
 * @return 条目字符串（环形表内，NUL 结尾）
 */
static const char *_hist_at(const bm_shell_t *shell, uint8_t nav) {
    uint8_t idx = (uint8_t)((shell->hist_head +
                             (uint8_t)BM_CONFIG_SHELL_HISTORY_DEPTH - nav) %
                            (uint8_t)BM_CONFIG_SHELL_HISTORY_DEPTH);
    return shell->hist[idx];
}

/**
 * @brief ↑：向更旧一条回翻并整行替换；无历史/已到最旧时响铃。
 */
static void _hist_up(bm_shell_t *shell) {
    if (shell->hist_count == 0u || shell->hist_nav >= shell->hist_count) {
        _puts("\a");
        return;
    }
    shell->hist_nav++;
    _line_replace(shell, _hist_at(shell, shell->hist_nav));
}

/**
 * @brief ↓：向更新一条回翻；越过最新 = 清空行；非浏览态无操作。
 */
static void _hist_down(bm_shell_t *shell) {
    if (shell->hist_nav == 0u) {
        return;
    }
    shell->hist_nav--;
    if (shell->hist_nav == 0u) {
        _line_replace(shell, NULL);
        return;
    }
    _line_replace(shell, _hist_at(shell, shell->hist_nav));
}

/**
 * @brief 非空行入环形历史；与最近一条相同则去重。
 *
 * 须在 bm_shell_exec 之前调用（exec 就地分词会改写行缓冲）。
 */
static void _hist_push(bm_shell_t *shell, const char *line) {
    size_t n;

    if (line[0] == '\0') {
        return;
    }
    if (shell->hist_count > 0u && _strcmp(_hist_at(shell, 1u), line) == 0) {
        return;
    }
    n = strlen(line);
    if (n > (size_t)(BM_CONFIG_SHELL_BUF_SIZE - 1)) {
        n = (size_t)(BM_CONFIG_SHELL_BUF_SIZE - 1);
    }
    (void)memcpy(shell->hist[shell->hist_head], line, n);
    shell->hist[shell->hist_head][n] = '\0';
    shell->hist_head = (uint8_t)((shell->hist_head + 1u) %
                                 (uint8_t)BM_CONFIG_SHELL_HISTORY_DEPTH);
    if (shell->hist_count < (uint8_t)BM_CONFIG_SHELL_HISTORY_DEPTH) {
        shell->hist_count++;
    }
}
#endif /* BM_CONFIG_SHELL_HISTORY_DEPTH > 0 */

/**
 * @brief 处理单个输入字符（回显、退格、Tab 补全、回车执行、模态 Ctrl+C）
 *
 * 模态期间仅识别 0x03（Ctrl+C）触发退出，**其余输入字符一律丢弃**
 * （静态单槽最简设计，见 bm_shell_modal_enter 契约）。
 *
 * @param shell Shell 实例指针
 * @param c 输入字符
 */
void bm_shell_feed(bm_shell_t *shell, char c) {
    if (!shell) return;

    /* 前台模态：只检 Ctrl+C，其余丢弃 */
    if (shell->modal_active) {
        if (c == 0x03) {
            _modal_exit(shell);
        }
        return;
    }

    if (c == '\n' && shell->swallow_lf) {
        shell->swallow_lf = 0;
        return;
    }
    shell->swallow_lf = 0;

    /* ESC 序列微状态机：吞 CSI（ESC[）/SS3（ESC O）序列——↑/↓ 触发历史，
     * 其余序列（←→/Home/End/Del…）静默吞掉，修「箭头键注入 [A 垃圾字符」。
     * 序列中出现非法字节（不在参数/终字节范围）：中止序列，该字节按普通
     * 字符继续走后续分支。 */
    if (shell->esc_state == 1u) {
        if (c == '[' || c == 'O') {
            shell->esc_state = 2u;
            return;
        }
        shell->esc_state = 0u; /* 独立 ESC：丢弃 ESC 本身，c 继续正常处理 */
    } else if (shell->esc_state == 2u) {
        unsigned char uc = (unsigned char)c;

        if (uc >= 0x20u && uc <= 0x3Fu) {
            return; /* 参数/中间字节（如 ESC[1;5A 的 1;5），继续吞 */
        }
        shell->esc_state = 0u;
        if (uc >= 0x40u && uc <= 0x7Eu) {
#if BM_CONFIG_SHELL_HISTORY_DEPTH > 0
            if (c == 'A') {
                _hist_up(shell);
            } else if (c == 'B') {
                _hist_down(shell);
            }
#endif
            return; /* 终字节：A/B 触发历史，其余静默吞掉 */
        }
        /* 非法字节：中止序列，按普通字符继续处理 */
    }
    if (c == 0x1B) {
        shell->esc_state = 1u;
        return;
    }

    if (c == '\b' || c == 0x7F) {
        if (shell->cursor > 0) {
#if BM_CONFIG_SHELL_HISTORY_DEPTH > 0
            shell->hist_nav = 0u; /* 编辑输入复位浏览态（buf 保留当前内容） */
#endif
            shell->cursor--;
            shell->buf[shell->cursor] = '\0';
            _puts("\b \b");
        }
        return;
    }

    if (c == '\t') {
#if BM_CONFIG_SHELL_HISTORY_DEPTH > 0
        shell->hist_nav = 0u; /* 编辑输入复位浏览态（buf 保留当前内容） */
#endif
        _tab_complete(shell);
        return;
    }

    if (c >= 0x20 && c < 0x7F) {
        if (shell->cursor < BM_CONFIG_SHELL_BUF_SIZE - 1) {
#if BM_CONFIG_SHELL_HISTORY_DEPTH > 0
            shell->hist_nav = 0u; /* 编辑输入复位浏览态（buf 保留当前内容） */
#endif
            shell->buf[shell->cursor++] = c;
            (void)bm_shell_port_write((const uint8_t *)&c, 1u);
        } else {
            BM_LOGW("shell", "line buffer full");
            (void)bm_shell_port_write(
                                       (const uint8_t *)"\a", 1u);
        }
        return;
    }

    if (c == '\r' || c == '\n') {
        if (c == '\r') {
            shell->swallow_lf = 1;
        }
        _puts("\r\n");
        if (shell->cursor > 0) {
            shell->buf[shell->cursor] = '\0';
#if BM_CONFIG_SHELL_HISTORY_DEPTH > 0
            _hist_push(shell, shell->buf); /* exec 就地分词改写 buf，先入史 */
            shell->hist_nav = 0u;
#endif
            bm_shell_exec(shell, shell->buf);
            shell->cursor = 0;
        }
        /* 命令若已进入前台模态，提示符留待模态退出时重绘 */
        if (!shell->modal_active) {
            _puts("$ ");
        }
    }
}

#ifndef BM_CONFIG_SHELL_MAX_CHARS_PER_POLL
#define BM_CONFIG_SHELL_MAX_CHARS_PER_POLL  64u
#endif

/**
 * @brief 轮询 Console CLI 接收并逐字符喂入 Shell
 *
 * 每轮最多处理 BM_CONFIG_SHELL_MAX_CHARS_PER_POLL 字符，
 * 保证主循环有界执行。剩余字符在后续 poll 中处理。
 *
 * 前台模态期间：输入仍逐字符喂入（feed 内只检 Ctrl+C、其余丢弃），
 * 处理完输入后若仍处于模态则调一次 tick 回调（限频由回调自理）。
 *
 * @param shell Shell 实例指针
 */
void bm_shell_poll(bm_shell_t *shell) {
    if (!shell) return;
    uint8_t c;
    uint32_t remaining = BM_CONFIG_SHELL_MAX_CHARS_PER_POLL;
    while (remaining > 0u &&
           bm_shell_port_read(&c, 1u) == 1u) {
        bm_shell_feed(shell, (char)c);
        remaining--;
    }
    if (shell->modal_active && shell->modal_tick) {
        shell->modal_tick(shell->modal_ctx);
    }
}

/**
 * @brief 通过 Shell 输出通道打印字符串
 *
 * @param s 以 NUL 结尾的字符串
 */
void bm_shell_puts(const char *s) {
    if (s) _puts(s);
}

/**
 * @brief 进入前台模态（契约详见头文件 bm_shell_modal_enter 注释）
 *
 * @param shell   Shell 实例指针
 * @param tick_fn 模态 tick 回调（必填）
 * @param stop_fn 模态 stop 回调（可为 NULL，仅限只读命令）
 * @param ctx     透传给回调的用户上下文
 * @return BM_OK 成功；BM_ERR_INVALID 参数无效；BM_ERR_BUSY 已处于模态
 */
int bm_shell_modal_enter(bm_shell_t *shell, bm_shell_modal_tick_fn_t tick_fn,
                         bm_shell_modal_stop_fn_t stop_fn, void *ctx) {
    if (!shell || !tick_fn) return BM_ERR_INVALID;
    if (shell->modal_active) {
        BM_LOGW("shell", "modal nested enter rejected");
        return BM_ERR_BUSY;
    }
    shell->modal_tick = tick_fn;
    shell->modal_stop = stop_fn;
    shell->modal_ctx = ctx;
    shell->modal_active = 1;
    BM_LOGD("shell", "modal enter");
    return BM_OK;
}

/**
 * @brief 查询是否处于前台模态
 *
 * @param shell Shell 实例指针
 * @return 非 0 处于模态；0 空闲或 shell 为空
 */
int bm_shell_modal_active(const bm_shell_t *shell) {
    return (shell && shell->modal_active) ? 1 : 0;
}
