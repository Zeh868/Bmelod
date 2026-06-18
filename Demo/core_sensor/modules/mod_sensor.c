#include "bm_log.h"
#include "bm_module.h"
#include "example_support.h"

#define TAG "core_sensor"

static int sensor_init(void) {
    example_print("[mod] sensor init\n");
    BM_LOGI(TAG, "module sensor init");
    return BM_OK;
}

static int sensor_start(void) {
    return BM_OK;
}

static int sensor_stop(void) {
    return BM_OK;
}

static int sensor_deinit(void) {
    return BM_OK;
}

BM_MODULE_DEFINE(sensor, 3,
    sensor_init, sensor_start, sensor_stop, sensor_deinit);
