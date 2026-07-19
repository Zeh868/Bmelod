/* 负例装配件：at >= every 必须触发 _Static_assert；try_compile 期望编译失败 */
#include "bm_tt_schedule.h"
#include "bm_bus.h"

BM_BUS_DEFINE(neg_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);
BM_BUS_DEFINE(neg_out_bus, uint32_t, 4u, 1u, BM_BUS_LATEST);

static bm_bus_t g_neg_bus;
static bm_bus_t g_neg_out_bus;
static const uint32_t k_safe = 0u;

static void neg_step(bm_let_ctx_t *c, void *s) { (void)c; (void)s; }

static const bm_let_input_t k_in[] = {
    { .bus = &g_neg_bus, .max_age_us = BM_LET_AGE_DEFAULT,
      .elem_size = sizeof(uint32_t), .safe_default = &k_safe } };
static const bm_let_output_t k_out[] = {
    { .bus = &g_neg_out_bus, .elem_size = sizeof(uint32_t),
      .safe_default = &k_safe } };

/* at=5 >= every=1：非法相位，须经 _Static_assert 在编译期失败 */
BM_LET_DEFINE_ISR(neg_task, 1u, 5u, 10u, neg_step, NULL, k_in, k_out);
BM_SCHEDULE_DEFINE(neg_sched, 1000u, &neg_task);

int main(void) { return 0; }
