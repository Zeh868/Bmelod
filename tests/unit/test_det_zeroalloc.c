/**
 * @file test_det_zeroalloc.c
 * @brief L1 零分配——全框架稳态练习在武装窗口（det_trap_arm..disarm）/ CRT 钩子下跑到结束
 *
 * B 块证据：在陷阱分配器武装窗口（GNU）或 CRT 钩子计数（MSVC）下，
 * 驱动 algorithm / bus / event / mempool / hrt-validate 稳态路径，
 * 任意动态分配即 abort（GNU）或计数≠0（MSVC），全通 = 零分配铁证。
 * 窗口外转发真实分配，放行 unity/printf 自身 I/O。
 *
 * 缺口：
 *   exec drain（BM_ENABLE_EXEC_TEST_HOOK 未找到，初始化链路依赖 HAL）→ C2 缺口。
 *   SMP IPC（BM_CONFIG_SMP=OFF 默认）→ A4 缺口。
 *
 * @author zeh (china_qzh@163.com)
 * @version 0.1
 * @date 2026-06-30
 */
#include "unity.h"
#include "bm/algorithm/bm_algo_motor.h"
#include "bm/algorithm/bm_algo_filter.h"
#include "bm/core/bm_bus.h"
#include "bm/core/bm_event.h"
#include "bm/core/bm_mempool.h"
#include "bm/hybrid/bm_hrt.h"
#include <stdint.h>
#include <string.h>

/* ---- 静态资源（编译期分配，不触发 malloc） ---- */

/** @brief 零分配练习用 bus LATEST 实例（容量 4，单读者） */
BM_BUS_DEFINE(det_za_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

/** @brief 零分配练习用 mempool：8 个 uint32_t 槽 */
BM_MEMPOOL_DEFINE(det_za_pool, uint32_t, 8u);

/** @brief 练习事件类型 ID（合法范围 [0,15]，取 14 避让低位 ID） */
#define DET_ZA_EVT_TYPE ((bm_event_type_t)0x0Eu)

/** @brief 占位事件回调（什么都不做，仅占订阅槽） */
static void det_za_noop_cb(const bm_event_t *ev, void *ud) { (void)ev; (void)ud; }

/**
 * @brief 全框架零分配稳态练习
 *
 * 调用 algorithm / bus / event / mempool / hrt 各层 API，
 * 不引入任何动态分配。全函数在陷阱分配器下正常返回即 = 零分配铁证。
 *
 * 注意：bus_storage 含 frozen 状态；同进程连续调用前须先 bus_reset。
 * 本函数由测试用例通过 setUp 保证仅调用一次（各 test 独立进程下 BSS 归零）。
 */
static void det_steady_state_exercise(void)
{
    /* ==== A：算法层 clarke（纯函数 canary） ==== */
    {
        bm_algo_abc_t       in;
        bm_algo_alphabeta_t ab;
        uint32_t            i;

        memset(&in, 0, sizeof(in));
        memset(&ab, 0, sizeof(ab));
        for (i = 0u; i < 200u; i++) {
            in.ia = (float)(int32_t)i * 0.01f;
            in.ib = -(float)(int32_t)i * 0.005f;
            in.ic = 0.0f;
            bm_algo_clarke(&in, &ab);
        }
    }

    /* ==== B：算法层 LPF1（有状态路径） ==== */
    {
        bm_algo_lpf1_config_t cfg;
        bm_algo_lpf1_state_t  st;
        uint32_t              i;

        memset(&cfg, 0, sizeof(cfg));
        (void)bm_algo_lpf1_init_from_cutoff(&cfg, 20.0f, 1000.0f);
        bm_algo_lpf1_reset(&st, 0.0f);
        for (i = 0u; i < 200u; i++) {
            (void)bm_algo_lpf1_step(&st, &cfg, (float)(int32_t)i * 0.005f);
        }
    }

    /* ==== C：bus LATEST publish + latest_read 100 次 ==== */
    {
        bm_bus_t     bus;
        bm_bus_cfg_t cfg;
        void        *wslot;
        uint32_t     readback;
        uint32_t     i;

        memset(&cfg, 0, sizeof(cfg));
        cfg.owner_cpu = 0u;
        (void)bm_bus_open(&bus, &det_za_bus_storage, &cfg);
        (void)bm_bus_freeze(&bus);
        for (i = 0u; i < 100u; i++) {
            (void)bm_bus_acquire_write(&bus, &wslot);
            *(uint32_t *)wslot = i;
            (void)bm_bus_commit(&bus);
            readback = 0u;
            (void)bm_bus_latest_read(&bus, &readback);
        }
        /* 先复位再 close：reset 操作 storage，须在 close 解绑前调用（与 Task4 同因） */
        (void)bm_bus_reset(&bus);
        (void)bm_bus_close(&bus);
    }

    /* ==== D：event register+subscribe+freeze+publish+process 50 次 ==== */
    {
        uint32_t i;

        for (i = 0u; i < 50u; i++) {
            bm_event_reset();
            (void)bm_event_register_type(DET_ZA_EVT_TYPE, "ZA_TEST");
            (void)bm_event_subscribe(DET_ZA_EVT_TYPE, det_za_noop_cb, NULL, NULL);
            bm_event_freeze_subscriptions();
            (void)bm_event_publish_copy(DET_ZA_EVT_TYPE, 0u, NULL, 0u);
            (void)bm_event_process(1u);
        }
    }

    /* ==== E：mempool alloc + free 100 次 ==== */
    {
        void    *ptrs[8u];
        uint32_t j;
        uint32_t i;

        bm_mempool_reset(&det_za_pool);
        for (i = 0u; i < 100u; i++) {
            /* 填满 8 槽 */
            for (j = 0u; j < 8u; j++) {
                ptrs[j] = bm_mempool_alloc(&det_za_pool);
            }
            /* 归还 8 槽 */
            for (j = 0u; j < 8u; j++) {
                bm_mempool_free(&det_za_pool, ptrs[j]);
            }
        }
    }

    /* ==== F：hrt 零槽 init + validate_period_us + reset ==== */
    {
        /* 零槽 init 合法（bm_hrt.h:60:"bm_hrt_init(NULL, 0) 是合法的零槽初始化"） */
        (void)bm_hrt_init(NULL, 0u);
        /* validate_period_us 是无副作用的纯校验 */
        (void)bm_hrt_validate_period_us(1000u);
        bm_hrt_reset();
    }
}

void setUp(void)    {}
void tearDown(void) {}

#ifdef _MSC_VER
#include <crtdbg.h>

/** MSVC CRT 钩子期间的分配计数 */
static long g_det_alloc_count;

/**
 * @brief CRT 分配钩子：统计 alloc/realloc 次数（MSVC native 计数路径）
 *
 * @param allocType 操作类型（_HOOK_ALLOC / _HOOK_REALLOC / _HOOK_FREE）
 * @param userData  用户数据（不使用）
 * @param size      请求大小（不使用）
 * @param blockType 块类型：过滤 _CRT_BLOCK，仅计客户区(_NORMAL_BLOCK)分配
 * @param requestNumber 请求序号（不使用）
 * @param filename  文件名（不使用）
 * @param lineNumber 行号（不使用）
 * @return 1 放行（仅计数，不拦截）
 */
static int det_alloc_hook(int allocType, void *userData, size_t size, int blockType,
                          long requestNumber, const unsigned char *filename, int lineNumber)
{
    (void)userData; (void)size;
    (void)requestNumber; (void)filename; (void)lineNumber;
    if ((allocType == _HOOK_ALLOC || allocType == _HOOK_REALLOC) && blockType != _CRT_BLOCK) {
        g_det_alloc_count++;
    }
    return 1;
}

/**
 * @brief MSVC：稳态运行期间 CRT 分配计数必须为 0
 *
 * 计数器围绕 det_steady_state_exercise() 装卸，
 * 结束后断言 alloc count == 0（smoke 路径，spec §4.2 A 方案）。
 */
void test_det_zero_alloc_msvc(void)
{
    _CRT_ALLOC_HOOK prev;

    g_det_alloc_count = 0;
    prev = _CrtSetAllocHook(det_alloc_hook);
    det_steady_state_exercise();
    (void)_CrtSetAllocHook(prev);
    TEST_ASSERT_EQUAL_INT32(0, g_det_alloc_count);
}
#else /* GNU —— abort-on-alloc 陷阱分配器由链接选项注入 */

/** 由 det_trap_alloc.c 提供：武装/解除窗口 */
void det_trap_arm(void);
void det_trap_disarm(void);

/**
 * @brief GNU：稳态运行——武装窗口内任一分配触发陷阱 abort；跑到断言即零分配铁证
 *
 * 权威路径（spec §4.2 B 方案）：链接 det_trap_alloc.c，
 * --wrap=malloc/calloc/realloc/free 重定向至可武装陷阱。
 * 预热 stdout 缓冲后武装陷阱、跑稳态练习、解除武装；
 * 练习期框架 BM_LOGI 复用已分配 stdout 缓冲、不再 malloc，
 * 故窗口内跑到结束 = 框架零动态分配（不可辩驳）。
 */
void test_det_zero_alloc_gnu(void)
{
    /* 预热宿主 stdio：武装前强制分配 stdout 缓冲，使练习期 fwrite 复用、不 malloc */
    printf("\n");
    fflush(stdout);

    det_trap_arm();
    det_steady_state_exercise();
    det_trap_disarm();

    TEST_ASSERT_TRUE(1); /* 武装窗口内陷阱未触发 = 框架零分配 */
}
#endif /* _MSC_VER */

int main(void)
{
    UNITY_BEGIN();
#ifdef _MSC_VER
    RUN_TEST(test_det_zero_alloc_msvc);
#else
    RUN_TEST(test_det_zero_alloc_gnu);
#endif
    return UNITY_END();
}
