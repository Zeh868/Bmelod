/**
 * @file test_qemu_stream_smoke.c
 * @brief QEMU 冒烟测试：静态零拷贝块流 bm_stream
 * @author zeh
 * @version 1.0
 * @date 2026-06-17
 */

#include "bm_stream.h"
#include "bm_hal_uart.h"
#include "bm_log.h"
#include <string.h>

#define PAYLOAD_SIZE 64
#define BLOCK_COUNT  4

BM_STREAM_PAYLOADS(my_stream, uint8_t, BLOCK_COUNT * PAYLOAD_SIZE);
BM_STREAM_BLOCKS(my_stream, BLOCK_COUNT);
BM_STREAM_INSTANCE(my_stream, BLOCK_COUNT);

int main(void) {
    bm_block_t *block;
    const char *line;
    int pass = 1;

    bm_hal_uart_init(NULL);
    BM_LOGI("qemu_stream", "start stream smoke test");

    if (bm_stream_init(&my_stream, _bm_stream_payload_my_stream, BLOCK_COUNT, PAYLOAD_SIZE) != BM_OK) {
        pass = 0;
    }

    if (pass) {
        /* Producer: acquire a free block */
        if (bm_stream_producer_acquire(&my_stream, &block) != BM_OK) {
            pass = 0;
        } else {
            /* Write some data and commit */
            memset(block->data, 0xAA, PAYLOAD_SIZE);
            bm_timestamp_t ts = {0};
            bm_stream_producer_commit(&my_stream, block, PAYLOAD_SIZE, &ts);
        }
    }

    if (pass) {
        /* Consumer: acquire the ready block */
        if (bm_stream_consumer_acquire(&my_stream, &block) != BM_OK) {
            pass = 0;
        } else {
            uint8_t *data = (uint8_t *)block->data;
            if (block->valid_bytes != PAYLOAD_SIZE || data[0] != 0xAA) {
                pass = 0;
            }
            bm_stream_consumer_release(&my_stream, block);
        }
    }

    if (pass) {
        line = "ok 1 - qemu_stream_smoke\n";
    } else {
        line = "not ok 1 - qemu_stream_smoke\n";
    }

    bm_hal_uart_send((const uint8_t *)line, strlen(line));
    while (1) {
    }
}
