#include "bm_module.h"

BM_MODULE_DECLARE(events);
BM_MODULE_DECLARE(display);
BM_MODULE_DECLARE(logger);
BM_MODULE_DECLARE(sensor);

BM_MODULE_TABLE(
    BM_MODULE_ENTRY(events),
    BM_MODULE_ENTRY(display),
    BM_MODULE_ENTRY(logger),
    BM_MODULE_ENTRY(sensor));
