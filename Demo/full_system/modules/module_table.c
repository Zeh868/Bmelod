#include "bm_module.h"

BM_MODULE_DECLARE(events);
BM_MODULE_DECLARE(fault);
BM_MODULE_DECLARE(comm);
BM_MODULE_DECLARE(sensor);
BM_MODULE_DECLARE(display);

BM_MODULE_TABLE(
    BM_MODULE_ENTRY(events),
    BM_MODULE_ENTRY(fault),
    BM_MODULE_ENTRY(comm),
    BM_MODULE_ENTRY(sensor),
    BM_MODULE_ENTRY(display));
