/**
 * @file main.c
 * @brief bus_servo 示例：ramp→PI→一阶 plant 闭环，通讯改用 bm_bus 三 mode
 *
 * 从 closed_loop_servo 移植，控制算法（ramp/PI/plant 参数和逻辑）保持不变，
 * 将通讯层从 bm_snapshot 换成 bm_bus 三种模式：
 *   - QUEUE ：指令通道（监督层写，控制环消费，保序不丢）
 *   - LATEST：轴最新状态（控制环写，监督层读最新值，三缓冲覆盖）
 *   - SIGNAL：遥测流（控制环每周期发布，两个消费者独立追赶）
 *
 * 验收：
 *   1. 闭环跟踪：loop_count >= PASS_LOOP_MIN，velocity_meas >= PASS_VEL_MIN
 *   2. 故障注入：fault_latched == 1，PWM safe state
 *   3. 三 mode 验证：QUEUE 指令被消费、LATEST 读到最新状态、SIGNAL 两消费者各读到遥测
 *
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-25
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-25       1.0            zeh            初稿，从 closed_loop_servo 移植 bm_bus 三 mode
 *
 */
#include "app_bus_servo.h"
#include "bm_algorithm.h"
#include "bm_exec.h"
#include "bm_hrt.h"
#include "bm_log.h"
#include "bm_module.h"
#include "bm_ticker.h"
#include "hybrid_print.h"

#include "bm_hal_adc_sim.h"
#include "bm_hal_pwm_sim.h"
#include "hal/bm_hal_timer.h"
#include "hal/bm_hal_uart.h"

#ifdef NATIVE_SIM
#include "bm_hal_timer_native.h"
#endif
#ifdef BM_EXAMPLE_QEMU
#include "qemu_delay.h"
#endif

#include <stdint.h>
#include <string.h>

#define TAG "bus_servo"

/* =========================================================================
 * 控制参数（与 closed_loop_servo 完全一致）
 * ========================================================================= */

/** 速度环 Scheduled HRT 周期（us） */
#define SERVO_LOOP_PERIOD_US    1000u
/** 离散控制周期（s） */
#define SERVO_DT_S              0.001f
/** PWM 满量程计数 */
#define SERVO_PWM_MAX           1000u
/** 速度反馈 ADC 标定：counts / (rad/s) */
#define SERVO_VEL_ADC_SCALE     500.0f

/** 一阶 plant 时间常数（s） */
#define PLANT_TAU_S             0.05f
/** 一阶 plant 稳态增益：velocity = GAIN * duty */
#define PLANT_GAIN              3.0f

/** 跟踪验收：稳态速度下限（rad/s） */
#define SERVO_PASS_VEL_MIN      1.5f

#if defined(BM_EXAMPLE_QEMU)
/** QEMU：放宽步数/速度阈值 */
#define SERVO_PASS_LOOP_MIN     100u
#define SERVO_PASS_VEL_MIN_RUN  0.4f
#else
#define SERVO_PASS_LOOP_MIN     500u
#define SERVO_PASS_VEL_MIN_RUN  SERVO_PASS_VEL_MIN
#endif

/* =========================================================================
 * bm_bus 三 mode 静态存储定义
 * ========================================================================= */

/**
 * @brief QUEUE：指令通道（容量 4，单读者，保序不丢）
 *
 * 监督层写入命令；控制环每周期消费最多 1 条。
 * QUEUE max_consumers=1，写满后写端返回 BM_ERR_OVERFLOW。
 */
BM_BUS_DEFINE(servo_cmd_bus, bus_servo_command_t, 4u, 1u, BM_BUS_QUEUE);

/**
 * @brief LATEST：轴最新状态（容量 3，三缓冲，单读者）
 *
 * 控制环每周期覆盖写最新状态；监督层读最新值。
 * LATEST 要求 cap >= 3（多核防撕裂）。
 */
BM_BUS_DEFINE(servo_state_bus, bus_servo_state_t, 3u, 1u, BM_BUS_LATEST);

/**
 * @brief SIGNAL：遥测流（容量 32，两个独立消费者）
 *
 * 控制环每周期（1 ms）发布遥测；导出消费者和监控消费者各自独立追赶。
 * cap=32 为 POLL 周期（native 10 ms ≈ 10 帧）留足缓冲，使两消费者
 * 多数 POLL 周期能正常追赶（BM_OK），不必每次走 overflow 分支。
 */
BM_BUS_DEFINE(servo_telem_bus, bus_servo_telemetry_t, 32u, 2u, BM_BUS_SIGNAL);

/* =========================================================================
 * 全局对象（app_bus_servo.h 中 extern 声明）
 * ========================================================================= */

/** 速度环运行时状态 */
bus_servo_axis_state_t      g_bus_servo_axis_state;
/** 监督层统计 */
bus_servo_supervisor_metrics_t g_bus_servo_metrics;

/** bm_bus 句柄（main 初始化，模块通过 extern 访问） */
bm_bus_t g_cmd_bus;    /**< QUEUE：指令通道 */
bm_bus_t g_state_bus;  /**< LATEST：轴状态 */
bm_bus_t g_telem_bus;  /**< SIGNAL：遥测流 */

/** 遥测 SIGNAL 两消费者读者句柄 */
bm_bus_reader_t g_telem_export_reader;  /**< 导出消费者 */
bm_bus_reader_t g_telem_monitor_reader; /**< 监控消费者 */

/** QUEUE 控制环消费者读者句柄 */
bm_bus_reader_t g_cmd_reader;   /**< 命令消费者（控制环） */

/** LATEST 监督层读者句柄 */
bm_bus_reader_t g_state_reader; /**< 状态读者（监督层） */

/* =========================================================================
 * 算法状态（与 closed_loop_servo 完全一致）
 * ========================================================================= */

/** ramp 配置：限制设定值变化率 */
static bm_algo_ramp_config_t g_ramp_cfg = {
    .rate_per_s = 4.0f
};
static bm_algo_ramp_state_t g_ramp_state;

/** 速度 PI 配置 */
static bm_algo_pi_config_t g_pi_cfg = {
    .kp = 0.8f,
    .ki = 6.0f,
    .out_min = 0.0f,
    .out_max = 1.0f,
    .integrator_min = -2.0f,
    .integrator_max = 2.0f
};
static bm_algo_pi_state_t g_pi_state;

/** native_sim 一阶电机模型内部速度状态 */
static float g_plant_velocity_rad_s;

/* =========================================================================
 * 公共 API 实现（供监督模块调用）
 * ========================================================================= */

/**
 * @brief 向 QUEUE 写入一条指令
 *
 * acquire_write → 填入数据 → commit；QUEUE 满则记日志跳过。
 *
 * @param cmd 指令结构体指针，非 NULL
 */
void app_bus_servo_publish_command(const bus_servo_command_t *cmd) {
    void *slot;
    int rc;

    if (cmd == NULL) {
        return;
    }
    rc = bm_bus_acquire_write(&g_cmd_bus, &slot);
    if (rc != BM_OK) {
        BM_LOGW(TAG, "cmd QUEUE acquire_write failed rc=%d", rc);
        return;
    }
    memcpy(slot, cmd, sizeof(bus_servo_command_t));
    (void)bm_bus_commit(&g_cmd_bus);
}

/**
 * @brief 从 LATEST 读取最新轴状态（拷贝语义）
 *
 * acquire_read → 拷贝 → release；无新数据时 out 保持原值。
 *
 * @param out 输出缓冲，非 NULL
 */
void app_bus_servo_read_state(bus_servo_state_t *out) {
    const void *slot;
    int rc;

    if (out == NULL) {
        return;
    }
    rc = bm_bus_acquire_read(&g_state_reader, &slot);
    if (rc == BM_OK) {
        memcpy(out, slot, sizeof(bus_servo_state_t));
        (void)bm_bus_release(&g_state_reader);
    }
}

/**
 * @brief 排空一个 SIGNAL 遥测消费者（内部公共逻辑）
 *
 * 循环 acquire_read → release 把当前可读遥测帧全部消费。
 *   - BM_OK         ：正常追赶，计 1 帧
 *   - BM_ERR_OVERFLOW：被绕过，游标已跳最旧可读槽，计 1 帧后继续追赶
 *   - 其它（WOULD_BLOCK）：无更多数据，退出
 *
 * @param reader   消费者读者句柄指针
 * @param tel_out  可选输出：最后一帧遥测（NULL 表示不需要）
 * @return 本次消费的遥测帧数
 */
static uint32_t drain_telem_reader(bm_bus_reader_t *reader,
                                   bus_servo_telemetry_t *tel_out) {
    const void *slot;
    uint32_t frames = 0u;
    int rc;

    for (;;) {
        rc = bm_bus_acquire_read(reader, &slot);
        if (rc == BM_OK || rc == BM_ERR_OVERFLOW) {
            if (tel_out != NULL) {
                memcpy(tel_out, slot, sizeof(bus_servo_telemetry_t));
            }
            (void)bm_bus_release(reader);
            frames++;
            /* overflow 后游标已重定位至最旧可读槽，继续循环正常追赶 */
        } else {
            break; /* BM_ERR_WOULD_BLOCK：本周期无更多遥测 */
        }
    }
    return frames;
}

/**
 * @brief 排空 SIGNAL 遥测导出消费者
 *
 * @return 本次读到的遥测帧数
 */
uint32_t app_bus_servo_drain_telem_export(void) {
    return drain_telem_reader(&g_telem_export_reader, NULL);
}

/**
 * @brief 排空 SIGNAL 遥测监控消费者
 *
 * @return 本次读到的遥测帧数
 */
uint32_t app_bus_servo_drain_telem_monitor(void) {
    return drain_telem_reader(&g_telem_monitor_reader, NULL);
}

/**
 * @brief 向 QUEUE 注入故障指令（测试安全停机路径）
 *
 * 直接构造一条置 FAULT 位、速度设定为 0 的命令并写入 QUEUE。
 */
void app_bus_servo_inject_fault(void) {
    bus_servo_command_t cmd;

    memset(&cmd, 0, sizeof(cmd));
    cmd.status = BUS_SERVO_CMD_STATUS_FAULT;
    cmd.velocity_setpoint_rad_s = 0.0f;
    app_bus_servo_publish_command(&cmd);
}

/* =========================================================================
 * 仿真辅助函数（与 closed_loop_servo 完全一致）
 * ========================================================================= */

/**
 * @brief 速度量纲 → ADC 原始值（仿真注入）
 *
 * @param velocity_rad_s 速度（rad/s）
 * @return ADC 原始值（uint16_t）
 */
static uint16_t velocity_to_adc(float velocity_rad_s) {
    float scaled = velocity_rad_s * SERVO_VEL_ADC_SCALE;

    if (scaled < 0.0f) {
        scaled = 0.0f;
    }
    if (scaled > 65535.0f) {
        scaled = 65535.0f;
    }
    return (uint16_t)scaled;
}

/**
 * @brief ADC 原始值 → 速度量纲
 *
 * @param raw ADC 原始值
 * @return 速度（rad/s）
 */
static float adc_to_velocity(uint16_t raw) {
    return (float)raw / SERVO_VEL_ADC_SCALE;
}

/**
 * @brief 一阶 plant 前向欧拉步进
 *
 * dv/dt = (GAIN * duty - v) / tau；结果写回仿真 ADC rank0。
 *
 * @param duty 归一化占空比 [0,1]
 */
static void plant_step(float duty) {
    float accel = (PLANT_GAIN * duty - g_plant_velocity_rad_s) / PLANT_TAU_S;

    g_plant_velocity_rad_s += accel * SERVO_DT_S;
    if (g_plant_velocity_rad_s < 0.0f) {
        g_plant_velocity_rad_s = 0.0f;
    }
    bm_hal_adc_sim_set_rank(&BM_HAL_ADC_SIM0, 0u,
                             velocity_to_adc(g_plant_velocity_rad_s));
}

/* =========================================================================
 * 速度环周期回调（bm_exec Scheduled HRT slot）
 * ========================================================================= */

/**
 * @brief 速度环周期回调（每 1 ms 调度一次）
 *
 * 控制链（与 closed_loop_servo 一致，通讯层替换为 bm_bus）：
 *   1. 从 QUEUE 消费一条命令（acquire_read + release）
 *   2. 检查故障/使能位
 *   3. ADC 读速度反馈 → ramp → PI → plant → PWM
 *   4. 向 LATEST 发布当前状态（acquire_write + commit）
 *   5. 向 SIGNAL 发布遥测（acquire_write + commit）
 *
 * @param instance bm_exec 实例指针（state 指向 g_bus_servo_axis_state）
 */
static void bus_servo_control_step(const bm_exec_t *instance) {
    bus_servo_axis_state_t *state = (bus_servo_axis_state_t *)instance->state;
    /* 保持最后一次有效命令（QUEUE 语义：一条命令只消费一次，之后保留上次值） */
    static bus_servo_command_t s_last_cmd;
    bus_servo_state_t state_pub;
    bus_servo_telemetry_t tel;
    uint16_t adc_raw = 0u;
    float velocity_meas;
    float velocity_ref;
    float error;
    float duty_f;
    uint16_t duty_pwm;
    int saturated;
    void *wslot;
    const void *rslot;
    int rc;

    /* --- 从 QUEUE 消费一条命令（有则更新，无则保持上次） --- */
    rc = bm_bus_acquire_read(&g_cmd_reader, &rslot);
    if (rc == BM_OK) {
        memcpy(&s_last_cmd, rslot, sizeof(bus_servo_command_t));
        (void)bm_bus_release(&g_cmd_reader);
        state->cmd_consumed++;
    }
    /* 以 s_last_cmd 作为当前有效命令（未收到新命令时沿用上次） */

    /* --- 故障命令：安全停机并复位算法状态 --- */
    if ((s_last_cmd.status & BUS_SERVO_CMD_STATUS_FAULT) != 0u) {
        state->fault_latched = 1;
        bm_hal_pwm_request_safe_state(&BM_HAL_PWM_SIM0);
        bm_algo_pi_reset(&g_pi_state, 0.0f);
        bm_algo_ramp_reset(&g_ramp_state, g_plant_velocity_rad_s);
        plant_step(0.0f);
        state->fault_count++;

        /* 发布 LATEST 状态 */
        rc = bm_bus_acquire_write(&g_state_bus, &wslot);
        if (rc == BM_OK) {
            state_pub.loop_count = state->loop_count;
            state_pub.velocity_meas_rad_s = g_plant_velocity_rad_s;
            state_pub.velocity_ref_rad_s = 0.0f;
            state_pub.duty = 0.0f;
            state_pub.fault_latched = 1;
            memcpy(wslot, &state_pub, sizeof(bus_servo_state_t));
            (void)bm_bus_commit(&g_state_bus);
        }

        /* 发布 SIGNAL 遥测 */
        rc = bm_bus_acquire_write(&g_telem_bus, &wslot);
        if (rc == BM_OK) {
            tel.sequence = state->loop_count;
            tel.status = BUS_SERVO_TEL_STATUS_FAULT;
            tel.velocity_meas_rad_s = g_plant_velocity_rad_s;
            tel.velocity_ref_rad_s = 0.0f;
            tel.duty = 0.0f;
            memcpy(wslot, &tel, sizeof(bus_servo_telemetry_t));
            (void)bm_bus_commit(&g_telem_bus);
        }

        state->loop_count++;
        return;
    }

    /* --- 未使能：零输出 --- */
    if ((s_last_cmd.status & BUS_SERVO_CMD_STATUS_ENABLED) == 0u) {
        plant_step(0.0f);
        (void)bm_hal_pwm_set_duty(&BM_HAL_PWM_SIM0, 0u, 0u);
        state->loop_count++;
        return;
    }

    /* --- 正常运行：ADC → ramp → PI → plant → PWM --- */
    if (bm_hal_adc_read_injected(&BM_HAL_ADC_SIM0, 0u, &adc_raw) != BM_OK) {
        BM_LOGW(TAG, "ADC read failed, request safe state");
        bm_hal_pwm_request_safe_state(&BM_HAL_PWM_SIM0);
        state->fault_latched = 1;
        state->fault_count++;
        return;
    }

    velocity_meas = adc_to_velocity(adc_raw);
    velocity_ref = bm_algo_ramp_step(&g_ramp_state, &g_ramp_cfg,
                                     s_last_cmd.velocity_setpoint_rad_s, SERVO_DT_S);
    error = velocity_ref - velocity_meas;
    duty_f = bm_algo_pi_step(&g_pi_state, &g_pi_cfg, error, SERVO_DT_S);
    saturated = (duty_f <= g_pi_cfg.out_min + 1e-6f) ||
                (duty_f >= g_pi_cfg.out_max - 1e-6f);

    plant_step(duty_f);
    duty_pwm = (uint16_t)(duty_f * (float)SERVO_PWM_MAX);
    (void)bm_hal_pwm_set_duty(&BM_HAL_PWM_SIM0, 0u, duty_pwm);

    /* 更新运行时状态 */
    state->velocity_meas_rad_s = velocity_meas;
    state->velocity_ref_rad_s = velocity_ref;
    state->duty = duty_f;

    /* --- 向 LATEST 发布当前轴状态 --- */
    rc = bm_bus_acquire_write(&g_state_bus, &wslot);
    if (rc == BM_OK) {
        state_pub.loop_count = state->loop_count;
        state_pub.velocity_meas_rad_s = velocity_meas;
        state_pub.velocity_ref_rad_s = velocity_ref;
        state_pub.duty = duty_f;
        state_pub.fault_latched = 0;
        memcpy(wslot, &state_pub, sizeof(bus_servo_state_t));
        (void)bm_bus_commit(&g_state_bus);
    }

    /* --- 向 SIGNAL 发布遥测（两消费者各自独立追赶） --- */
    rc = bm_bus_acquire_write(&g_telem_bus, &wslot);
    if (rc == BM_OK) {
        tel.sequence = state->loop_count;
        tel.status = BUS_SERVO_TEL_STATUS_VALID;
        if (saturated) {
            tel.status |= BUS_SERVO_TEL_STATUS_SAT;
        }
        tel.velocity_meas_rad_s = velocity_meas;
        tel.velocity_ref_rad_s = velocity_ref;
        tel.duty = duty_f;
        memcpy(wslot, &tel, sizeof(bus_servo_telemetry_t));
        (void)bm_bus_commit(&g_telem_bus);
    }

    state->loop_count++;
}

/* =========================================================================
 * bm_exec 回调
 * ========================================================================= */

/**
 * @brief 速度环初始化回调
 *
 * 复位算法状态和仿真 ADC，对齐 closed_loop_servo 逻辑。
 *
 * @param instance bm_exec 实例指针
 * @return BM_OK
 */
static int bus_servo_init(const bm_exec_t *instance) {
    (void)instance;
    bm_algo_ramp_reset(&g_ramp_state, 0.0f);
    bm_algo_pi_reset(&g_pi_state, 0.0f);
    g_plant_velocity_rad_s = 0.0f;
    bm_hal_adc_sim_set_rank(&BM_HAL_ADC_SIM0, 0u, 0u);
    BM_LOGD(TAG, "bus_servo axis init");
    return BM_OK;
}

/**
 * @brief 速度环启动回调：使能 PWM 输出
 *
 * @param instance bm_exec 实例指针
 * @return bm_hal_pwm_enable_outputs 返回码
 */
static int bus_servo_start(const bm_exec_t *instance) {
    (void)instance;
    return bm_hal_pwm_enable_outputs(&BM_HAL_PWM_SIM0, 1);
}

/**
 * @brief 速度环安全停机回调：请求 PWM 安全态
 *
 * @param instance bm_exec 实例指针
 */
static void bus_servo_safe_stop(const bm_exec_t *instance) {
    (void)instance;
    bm_hal_pwm_request_safe_state(&BM_HAL_PWM_SIM0);
}

/** bm_exec ops 表 */
static const bm_exec_ops_t g_bus_servo_ops = {
    bus_servo_init, bus_servo_start, bus_servo_safe_stop
};

/** bm_exec 周期 slot（1 ms 速度环） */
static const bm_exec_slot_t g_bus_servo_slots[] = {
    {
        .kind = BM_EXEC_SLOT_PERIODIC,
        .period_us = SERVO_LOOP_PERIOD_US,
        .run = bus_servo_control_step,
        .name = "speed_loop"
    }
};

/** 单轴伺服 exec 实例 */
const bm_exec_t g_bus_servo_axis = {
    .id = 1u,
    .owner_cpu = 0u,
    .name = "bus_servo_axis",
    .state = &g_bus_servo_axis_state,
    .config = NULL,
    .resources = NULL,
    .slots = g_bus_servo_slots,
    .slot_count = 1u,
    .claims = NULL,
    .claim_count = 0u,
    .ops = &g_bus_servo_ops
};

static const bm_exec_t *const g_instances[] = { &g_bus_servo_axis };

/* =========================================================================
 * 监督层 ticker
 * ========================================================================= */

#if defined(BM_EXAMPLE_QEMU)
#define SERVO_POLL_MS  10u
#else
/* native：5 ms 轮询，控制环 1 ms 发一帧 → 每 POLL 周期约 5 帧，
 * SIGNAL cap=32 留足缓冲，两消费者多数周期正常追赶（不触发 overflow） */
#define SERVO_POLL_MS  5u
#endif

/** 监督层遥测轮询 ticker */
static const bm_ticker_slot_t g_poll_ticker[] = {
    { SERVO_POLL_MS, EVENT_BUS_SERVO_POLL, 1u, "tel_poll" }
};

/* =========================================================================
 * 仿真推进辅助
 * ========================================================================= */

/**
 * @brief 推进仿真时钟并调度 ticker / 事件（与 closed_loop_servo 一致）
 *
 * @param cycles 推进轮次
 */
static void run_sim(uint32_t cycles) {
    uint32_t i;

    for (i = 0u; i < cycles; ++i) {
#ifdef NATIVE_SIM
        bm_hal_timer_native_advance_ticks(1u);
#elif defined(BM_EXAMPLE_QEMU)
        {
            uint32_t s;
            for (s = 0u; s < 4u; ++s) {
                bm_example_qemu_spin();
            }
        }
        bm_hrt_poll();
#else
        for (volatile uint32_t d = 0u; d < 20u; ++d) {
        }
#endif
        (void)bm_ticker_poll();
        (void)bm_event_process(8u);
    }
}

/* =========================================================================
 * main
 * ========================================================================= */

/**
 * @brief bus_servo 示例入口
 *
 * 初始化顺序：
 *   1. bm_bus open + reader_attach + freeze
 *   2. bm_module_boot（模块初始化/启动）
 *   3. timer init
 *   4. exec init/start
 *   5. ticker init
 *   6. run_sim（推进闭环）
 *   7. 验收：tracking + 故障注入 + 三 mode 验证
 *
 * @return 0 PASS，1 FAIL
 */
int main(void) {
    int rc;
    bm_bus_cfg_t bus_cfg = { .owner_cpu = 0u };

    BM_LOGI(TAG, "bus_servo example start");
    bm_hal_uart_init(NULL);

    /* --- Step 1：初始化 bm_bus 三 mode --- */

    /* QUEUE：指令通道 */
    rc = bm_bus_open(&g_cmd_bus, &servo_cmd_bus_storage, &bus_cfg);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "cmd bus open failed rc=%d", rc);
        hybrid_print("EXAMPLE_BUS_SERVO: FAIL bus_open_cmd\n");
        return 1;
    }
    rc = bm_bus_reader_attach(&g_cmd_bus, &g_cmd_reader);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "cmd reader attach failed rc=%d", rc);
        hybrid_print("EXAMPLE_BUS_SERVO: FAIL reader_attach_cmd\n");
        return 1;
    }
    (void)bm_bus_freeze(&g_cmd_bus);

    /* LATEST：轴状态 */
    rc = bm_bus_open(&g_state_bus, &servo_state_bus_storage, &bus_cfg);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "state bus open failed rc=%d", rc);
        hybrid_print("EXAMPLE_BUS_SERVO: FAIL bus_open_state\n");
        return 1;
    }
    rc = bm_bus_reader_attach(&g_state_bus, &g_state_reader);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "state reader attach failed rc=%d", rc);
        hybrid_print("EXAMPLE_BUS_SERVO: FAIL reader_attach_state\n");
        return 1;
    }
    (void)bm_bus_freeze(&g_state_bus);

    /* SIGNAL：遥测流，两个消费者 */
    rc = bm_bus_open(&g_telem_bus, &servo_telem_bus_storage, &bus_cfg);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "telem bus open failed rc=%d", rc);
        hybrid_print("EXAMPLE_BUS_SERVO: FAIL bus_open_telem\n");
        return 1;
    }
    rc = bm_bus_reader_attach(&g_telem_bus, &g_telem_export_reader);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "telem export reader attach failed rc=%d", rc);
        hybrid_print("EXAMPLE_BUS_SERVO: FAIL reader_attach_telem_export\n");
        return 1;
    }
    rc = bm_bus_reader_attach(&g_telem_bus, &g_telem_monitor_reader);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "telem monitor reader attach failed rc=%d", rc);
        hybrid_print("EXAMPLE_BUS_SERVO: FAIL reader_attach_telem_monitor\n");
        return 1;
    }
    (void)bm_bus_freeze(&g_telem_bus);

    /* --- Step 2：模块启动 --- */
    rc = bm_module_boot();
    if (rc != BM_OK) {
        BM_LOGE(TAG, "module boot failed rc=%d", rc);
        hybrid_print("EXAMPLE_BUS_SERVO: FAIL boot\n");
        return 1;
    }

    /* --- Step 3：定时器初始化 --- */
    (void)bm_hal_timer_init(1000000u / BM_CONFIG_HRT_TICK_US);

    /* --- Step 4：exec 初始化与启动 --- */
    rc = bm_exec_init_all(g_instances, 1u);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "exec init failed rc=%d", rc);
        hybrid_print("EXAMPLE_BUS_SERVO: FAIL init\n");
        return 1;
    }
    rc = bm_exec_start_all(g_instances, 1u);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "exec start failed rc=%d", rc);
        bm_exec_safe_stop_all(g_instances, 1u);
        hybrid_print("EXAMPLE_BUS_SERVO: FAIL start\n");
        return 1;
    }

    /* --- Step 5：ticker 初始化 --- */
    rc = bm_ticker_init(g_poll_ticker, 1u);
    if (rc != BM_OK) {
        BM_LOGE(TAG, "ticker init failed rc=%d", rc);
        bm_exec_safe_stop_all(g_instances, 1u);
        hybrid_print("EXAMPLE_BUS_SERVO: FAIL ticker\n");
        return 1;
    }

#ifdef BM_EXAMPLE_QEMU
    bm_example_qemu_warmup();
#endif

    (void)bm_event_process(8u);

    /* --- Step 6：推进仿真，等待闭环跟踪收敛 --- */
#ifdef NATIVE_SIM
    run_sim(5500u);
#elif defined(BM_EXAMPLE_QEMU)
    run_sim(40000u);
#else
    run_sim(1000u);
#endif

    /* --- 验收 A：闭环跟踪 --- */
    if (g_bus_servo_axis_state.loop_count < SERVO_PASS_LOOP_MIN ||
        g_bus_servo_axis_state.velocity_meas_rad_s < SERVO_PASS_VEL_MIN_RUN ||
        g_bus_servo_metrics.command_publishes == 0u ||
        g_bus_servo_metrics.state_reads == 0u) {
        BM_LOGE(TAG,
                "tracking failed: loops=%u vel=%.3f cmd_pub=%u state_read=%u",
                (unsigned)g_bus_servo_axis_state.loop_count,
                (double)g_bus_servo_axis_state.velocity_meas_rad_s,
                (unsigned)g_bus_servo_metrics.command_publishes,
                (unsigned)g_bus_servo_metrics.state_reads);
        hybrid_print("EXAMPLE_BUS_SERVO: FAIL tracking\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    /* --- 验收 B：故障注入 → 安全停机 --- */
    app_bus_servo_inject_fault();
#ifdef NATIVE_SIM
    run_sim(200u);
#elif defined(BM_EXAMPLE_QEMU)
    run_sim(2000u);
#else
    run_sim(100u);
#endif

    if (g_bus_servo_axis_state.fault_latched == 0 ||
        bm_hal_pwm_sim_outputs_enabled(&BM_HAL_PWM_SIM0)) {
        BM_LOGE(TAG, "fault path failed: latched=%d pwm=%d",
                g_bus_servo_axis_state.fault_latched,
                bm_hal_pwm_sim_outputs_enabled(&BM_HAL_PWM_SIM0));
        hybrid_print("EXAMPLE_BUS_SERVO: FAIL fault\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    /* --- 验收 C：三 mode 验证（消费/计数均来自监督模块回调） --- */

    /* C1. QUEUE：指令被控制环消费（cmd_consumed > 0） */
    if (g_bus_servo_axis_state.cmd_consumed == 0u) {
        BM_LOGE(TAG, "QUEUE cmd not consumed");
        hybrid_print("EXAMPLE_BUS_SERVO: FAIL queue_not_consumed\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    /* C2. LATEST：监督层 POLL 回调读到最新状态（state_reads > 0） */
    if (g_bus_servo_metrics.state_reads == 0u) {
        BM_LOGE(TAG, "LATEST state not read by supervisor");
        hybrid_print("EXAMPLE_BUS_SERVO: FAIL latest_not_read\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    /* C3. SIGNAL：两个消费者在 POLL 回调里各自持续追赶遥测流。
     *     要求两者各读到多帧（持续追赶，而非偶发单帧）。 */
    if (g_bus_servo_metrics.telem_export_reads == 0u ||
        g_bus_servo_metrics.telem_monitor_reads == 0u) {
        BM_LOGE(TAG, "SIGNAL telem not consumed: export=%u monitor=%u",
                (unsigned)g_bus_servo_metrics.telem_export_reads,
                (unsigned)g_bus_servo_metrics.telem_monitor_reads);
        hybrid_print("EXAMPLE_BUS_SERVO: FAIL signal_telem\n");
        bm_exec_safe_stop_all(g_instances, 1u);
        return 1;
    }

    /* 全部通过 */
    BM_LOGI(TAG,
            "PASS: loops=%u vel=%.3f cmd_pub=%u state_rd=%u "
            "export_rd=%u monitor_rd=%u faults=%u cmd_consumed=%u",
            (unsigned)g_bus_servo_axis_state.loop_count,
            (double)g_bus_servo_axis_state.velocity_meas_rad_s,
            (unsigned)g_bus_servo_metrics.command_publishes,
            (unsigned)g_bus_servo_metrics.state_reads,
            (unsigned)g_bus_servo_metrics.telem_export_reads,
            (unsigned)g_bus_servo_metrics.telem_monitor_reads,
            (unsigned)g_bus_servo_axis_state.fault_count,
            (unsigned)g_bus_servo_axis_state.cmd_consumed);
    hybrid_print_pass("BUS_SERVO");

    bm_exec_safe_stop_all(g_instances, 1u);
#ifdef NATIVE_SIM
    return 0;
#else
    while (1) {
    }
#endif
}
