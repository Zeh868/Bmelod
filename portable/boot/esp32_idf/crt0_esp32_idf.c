/**
 * @file crt0_esp32_idf.c
 * @brief ESP32 裸机运行时初始化骨架
 *
 * 该文件只负责数据段搬运、BSS 清零与进入应用入口，
 * 不创建任务，也不依赖任何调度器。
 */

#include <stddef.h>
#include <stdint.h>

extern uint8_t _sdata;
extern uint8_t _edata;
extern uint8_t _sidata;
extern uint8_t _sbss;
extern uint8_t _ebss;

extern int main(void);

/**
 * @brief 裸机启动入口。
 *
 * @return 不返回。
 */
__attribute__((noreturn)) void bm_esp32_baremetal_runtime_start(void) {
    uint8_t *dst;
    const uint8_t *src;

    src = &_sidata;
    for (dst = &_sdata; dst < &_edata; ++dst, ++src) {
        *dst = *src;
    }
    for (dst = &_sbss; dst < &_ebss; ++dst) {
        *dst = 0u;
    }

    (void)main();
    for (;;) {
        __asm__ volatile ("waiti 0");
    }
}
