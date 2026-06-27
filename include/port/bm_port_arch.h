/* SPDX-License-Identifier: GPL-3.0-or-later */
/**
 * @file bm_port_arch.h
 * @brief 架构 Port ID 字符串常量
 *
 * 与 CMake 变量 `BM_PORT_ARCH` 及 `portable/arch/<id>/` 目录名一一对应。
 * @author zeh (china_qzh@163.com)
 * @version 1.0
 * @date 2026-06-14
 *
 * @par 修改日志:
 *
 *    Date         Version        Author          Description
 * 2026-06-14       1.0            zeh            正式发布
 *
 */
#ifndef BM_PORT_ARCH_H
#define BM_PORT_ARCH_H

/** 全桩 arch（CI / 无硬件） */
#define BM_PORT_ARCH_STUB        "stub"
/** 宿主仿真（PC native_sim） */
#define BM_PORT_ARCH_HOST        "host"
/** ARMv6-M：Cortex-M0/M0+ */
#define BM_PORT_ARCH_ARMV6M      "armv6m"
/** ARMv7-M：Cortex-M3 */
#define BM_PORT_ARCH_ARMV7M      "armv7m"
/** ARMv7E-M：Cortex-M4/M4F/M7 */
#define BM_PORT_ARCH_ARMV7EM     "armv7em"
/** ARMv8-M Baseline：Cortex-M23 */
#define BM_PORT_ARCH_ARMV8M_BASE "armv8m_base"
/** ARMv8-M Mainline：Cortex-M33/M55 等 */
#define BM_PORT_ARCH_ARMV8M_MAIN "armv8m_main"
/** RISC-V 32 位：RV32IMAC ilp32 */
#define BM_PORT_ARCH_RISCV32     "riscv32"
/** RISC-V 32 位 + F 扩展 */
#define BM_PORT_ARCH_RISCV32F    "riscv32f"
/** RISC-V 64 位：RV64IMAC lp64 */
#define BM_PORT_ARCH_RISCV64     "riscv64"
/** RISC-V 64 位 + D 扩展 */
#define BM_PORT_ARCH_RISCV64F    "riscv64f"
/** Xtensa：ESP32 等 */
#define BM_PORT_ARCH_XTENSA      "xtensa"

#endif /* BM_PORT_ARCH_H */
